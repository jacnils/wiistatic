#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <malloc.h> // for memalign
#include <sys/stat.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <aesndlib.h>
#include <fat.h>

constexpr int TEX_W = 320;
constexpr int TEX_H = 240;
constexpr int TEX_BYTES = TEX_W * TEX_H;
constexpr int TEX_WORDS = TEX_BYTES / 4;

constexpr int AUDIO_RATE = 32000;
constexpr int AUDIO_SECONDS = 2;
constexpr int AUDIO_SAMPLES = AUDIO_RATE * AUDIO_SECONDS;
constexpr int AUDIO_BYTES = AUDIO_SAMPLES * 2;

constexpr uint32_t FIFO_SIZE = 256 * 1024;

struct Rng {
    uint32_t state = 0x1234ABCD;

    uint32_t next() {
        uint32_t x = state;

        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;

        state = x;
        return x;
    }

    Rng() = default;
    Rng(uint32_t state) : state(state) {}
};

Rng rng{};

volatile bool want_exit{false};
volatile bool want_poweroff{false};

struct Settings {
    int volume = 120; // 0..255
    bool muted = false; // muted, self explanatory
    bool band = false; // band going down the screen
};

constexpr long long make_title_id(const std::string& ascii) noexcept {
    if (ascii.size() != 4) {
        return 0;
    }

    long long id = 0x0001000100000000LL;
    for (size_t i = 0; i < 4; ++i) {
        id |= static_cast<long long>(static_cast<unsigned char>(ascii[i])) << (8 * (3 - i));
    }
    
    return id;
}
enum class title_id : long long {
    HAXX = make_title_id("HAXX"),
    JODI = make_title_id("JODI"),
    LULZ = make_title_id("LULZ"),
    OHBC = make_title_id("OHBC"),
};

class Screen {
public:
    explicit Screen() { // boilerplate shit
        VIDEO_Init();
        
        rmode_ = VIDEO_GetPreferredMode(nullptr);
        
        xfb_[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode_));
        xfb_[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode_));
        
        VIDEO_Configure(rmode_);
        VIDEO_SetNextFramebuffer(xfb_[fb_]);
        VIDEO_SetBlack(false);
        VIDEO_Flush();
        VIDEO_WaitVSync();

        if (rmode_->viTVMode & VI_NON_INTERLACE)
            VIDEO_WaitVSync();

        init_gx();

        noise_ = static_cast<uint32_t *>(memalign(32, TEX_BYTES));

        std::memset(noise_, 0, TEX_BYTES);
    }

    void render_frame() {
        update_noise();

        textured_mode();
        const uint8_t lum = static_cast<uint8_t>(225 + (rng.next() % 31));
        quad(0, 0, 640, 480, lum, lum, lum, 255, 255);

        if (has_band_) {
            color_mode();
            band_y_ += band_speed;
            if (band_y_ > 480.0f + band_h) band_y_ -= 480.0f + band_h;
            quad(0, band_y_ - band_h, 640, band_y_, 255, 255, 255, 0, 70);
        }

        GX_DrawDone();
        
        fb_ ^= 1;
        
        GX_CopyDisp(xfb_[fb_], GX_TRUE);
        
        VIDEO_SetNextFramebuffer(xfb_[fb_]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
    }

    void toggle_band() { has_band_ = !has_band_; }
    void set_band(bool on) { has_band_ = on; }
    bool band() const { return has_band_; }

    void shutdown() {
        GX_AbortFrame();
        GX_Flush();
        
        VIDEO_SetBlack(true);
        VIDEO_Flush();
    }

private:
    void init_gx() {
        gpfifo_ = memalign(32, FIFO_SIZE);
        std::memset(gpfifo_, 0, FIFO_SIZE);
        GX_Init(gpfifo_, FIFO_SIZE);

        GXColor black = {0, 0, 0, 255};
        GX_SetCopyClear(black, GX_MAX_Z24);
        GX_SetViewport(0, 0, rmode_->fbWidth, rmode_->efbHeight, 0, 1);

        f32 yscale = GX_GetYScaleFactor(rmode_->efbHeight, rmode_->xfbHeight);
        uint32_t xfbHeight = GX_SetDispCopyYScale(yscale);
        GX_SetScissor(0, 0, rmode_->fbWidth, rmode_->efbHeight);
        GX_SetDispCopySrc(0, 0, rmode_->fbWidth, rmode_->efbHeight);
        GX_SetDispCopyDst(rmode_->fbWidth, xfbHeight);
        GX_SetCopyFilter(rmode_->aa, rmode_->sample_pattern, GX_TRUE, rmode_->vfilter);
        GX_SetFieldMode(rmode_->field_rendering,
                        (rmode_->viHeight == 2 * rmode_->xfbHeight) ? GX_ENABLE : GX_DISABLE);
        GX_SetCullMode(GX_CULL_NONE);
        GX_SetDispCopyGamma(GX_GM_1_0);
        GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(xfb_[fb_], GX_TRUE);

        Mtx44 proj;
        guOrtho(proj, 0, 480, 0, 640, 0, 1);
        GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
        Mtx mv;
        guMtxIdentity(mv);
        GX_LoadPosMtxImm(mv, GX_PNMTX0);

        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

        GX_SetNumChans(1);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    }

    void textured_mode() {
        GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    }

    void color_mode() {
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    }

    void quad(f32 x0, f32 y0, f32 x1, f32 y1, uint8_t r, uint8_t g, uint8_t b, uint8_t aTop, uint8_t aBot) {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x0, y0); GX_Color4u8(r, g, b, aTop); GX_TexCoord2f32(0, 0);
        GX_Position2f32(x1, y0); GX_Color4u8(r, g, b, aTop); GX_TexCoord2f32(1, 0);
        GX_Position2f32(x1, y1); GX_Color4u8(r, g, b, aBot); GX_TexCoord2f32(1, 1);
        GX_Position2f32(x0, y1); GX_Color4u8(r, g, b, aBot); GX_TexCoord2f32(0, 1);
        GX_End();
    }

    void update_noise() {
        for (int i = 0; i < TEX_WORDS; i++) 
            noise_[i] = rng.next();
        
        DCFlushRange(noise_, TEX_BYTES);
        
        GX_InvalidateTexAll();
        GX_InitTexObj(&tex_obj, noise_, TEX_W, TEX_H, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&tex_obj, GX_NEAR, GX_NEAR);
        GX_LoadTexObj(&tex_obj, GX_TEXMAP0);
    }

    GXRModeObj *rmode_ = nullptr;
    
    void *xfb_[2] = {nullptr, nullptr};
    int fb_ = 0;
    void *gpfifo_ = nullptr;
    
    uint32_t *noise_ = nullptr;
    GXTexObj tex_obj;

    bool has_band_ = true;
    f32 band_y_ = 0.0f;
    f32 band_speed = 3.0f;
    f32 band_h = 140.0f;
};

class AudioPlayer {
public:
    void start(const Settings &s) {
        volume_ = s.volume;
        muted_ = s.muted;

        buf_ = static_cast<s16 *>(memalign(32, AUDIO_BYTES));

        for (int i = 0; i < AUDIO_SAMPLES; i++) {
            buf_[i] = static_cast<s16>(static_cast<s16>(rng.next() >> 16) >> 2);
        }

        DCFlushRange(buf_, AUDIO_BYTES);

        AESND_Init();
        AESND_Pause(false);

        voice_ = AESND_AllocateVoice([](AESNDPB*, uint32_t) {});

        AESND_SetVoiceVolume(voice_, static_cast<u16>(volume_), static_cast<u16>(volume_));
        AESND_PlayVoice(voice_, VOICE_MONO16, buf_, AUDIO_BYTES, AUDIO_RATE, 0, true);

        if (muted_)
            AESND_SetVoiceStop(voice_, true);
    }

    void toggle_mute() {
        muted_ = !muted_;
        AESND_SetVoiceStop(voice_, muted_);
    }

    void change_vol(int delta) {
        volume_ += delta;
        if (volume_ < 0) volume_ = 0;
        if (volume_ > 255) volume_ = 255;
        AESND_SetVoiceVolume(voice_, static_cast<u16>(volume_), static_cast<u16>(volume_));
    }

    int volume() const { return volume_; }
    bool muted() const { return muted_; }

    void stop() {
        if (voice_) AESND_SetVoiceStop(voice_, true);
        AESND_Pause(true);
    }

private:
    s16 *buf_ = nullptr;
    AESNDPB *voice_ = nullptr;
    int volume_ = 120;
    bool muted_ = false;
};

int main(int argc, char **argv) {
    Settings settings{};

    Screen screen;
    screen.set_band(settings.band);

    WPAD_Init();
    PAD_Init();

    SYS_SetResetCallback([](uint32_t, void*) { want_exit = true; });
    SYS_SetPowerCallback([]() { want_poweroff = true; });

    AudioPlayer audio;
    audio.start(settings);

    // mostly poll controls
    while (!want_exit && !want_poweroff) {
        WPAD_ScanPads();
        PAD_ScanPads();

        const uint32_t down = WPAD_ButtonsDown(0);
        const uint32_t padDown = PAD_ButtonsDown(0);

        if ((down & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) || (padDown & PAD_BUTTON_START)) {
            break;
        }

        if (down & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))
            audio.toggle_mute();

        if ((down & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) || (padDown & PAD_BUTTON_B)) {
            screen.toggle_band();
        }

        if (down & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS))
            audio.change_vol(20);

        if (down & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS))
            audio.change_vol(-20);

        screen.render_frame();
    }

    settings.volume = audio.volume();
    settings.muted = audio.muted();
    settings.band = screen.band();
    
    audio.stop();
    screen.shutdown();

    if (want_poweroff)
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);

    // just iterate between most loaders
    // TODO: is there a better way to do this?
    // just ripped this code from ff-wii
    for (const auto& it : { title_id::HAXX, title_id::JODI, title_id::LULZ, title_id::OHBC }) {
        try {
            WII_LaunchTitle(static_cast<long long>(it));
        } catch (const std::runtime_error&) {} // just retry
    }

    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);

    return 0;
}