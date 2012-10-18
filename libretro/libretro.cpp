#include "libretro.h"

#include "MMU.h"
#include "NDSSystem.h"
#include "debug.h"
#include "sndsdl.h"
#include "render3D.h"
#include "rasterize.h"
#include "saves.h"
#include "firmware.h"
#include "GPU_osd.h"
#include "addons.h"

//

static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
static retro_input_state_t input_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

//

volatile bool execute = false;

// Video buffer
static uint16_t screenSwap[256 * 192 * 2];

#ifndef SWAPTYPE
# define SWAPTYPE uint32_t
#endif

namespace VIDEO
{
    template<typename T>
    void SwapScreen(void* aOut, const void* aIn)
    {
        static const uint32_t pixPerT = sizeof(T) / 2;
        static const uint32_t totalPix = 256 * 192 * 2 / pixPerT;
        static const T rMask = (pixPerT == 1) ? 0xF800 : ((pixPerT == 2) ? 0xF800F800 : 0xF800F800F800F800);
        static const T gMask = (pixPerT == 1) ? 0x07C0 : ((pixPerT == 2) ? 0x07C007C0 : 0x07C007C007C007C0);
        static const T bMask = (pixPerT == 1) ? 0x001F : ((pixPerT == 2) ? 0x001F001F : 0x001F001F001F001F);
        
        assert(pixPerT == 1 || pixPerT == 2 || pixPerT == 4);
        
        const T* inPix = (const T*)aIn;
        T* outPix = (T*)aOut;
        
        for(int i = 0; i != totalPix; i ++)
        {
            const T p = *inPix++;
            
            const T r = (p & rMask) >> 10;
            const T g = (p & gMask);
            const T b = (p & bMask) << 10;
            
            *outPix++ = r | g | b;
        }
    }
}

namespace AUDIO
{
    static unsigned frames;
    
    int SNDRetroInit(int buffersize){return 0;}
    void SNDRetroDeInit(){}
    void SNDRetroMuteAudio(){}
    void SNDRetroUnMuteAudio(){}
    void SNDRetroSetVolume(int volume){}
    
    
    void SNDRetroUpdateAudio(s16 *buffer, u32 num_samples)
    {
        audio_batch_cb(buffer, num_samples);
        frames += num_samples;
    }
    
    u32 SNDRetroGetAudioSpace(){return 735 - frames;}
    
    const int SNDCORE_RETRO = 2000;
    SoundInterface_struct SNDRetro =
    {
        SNDCORE_RETRO,
        "libretro Sound Interface",
        SNDRetroInit,
        SNDRetroDeInit,
        SNDRetroUpdateAudio,
        SNDRetroGetAudioSpace,
        SNDRetroMuteAudio,
        SNDRetroUnMuteAudio,
        SNDRetroSetVolume
    };
}

namespace INPUT
{
    template<int min, int max>
    static int32_t ClampedMove(int32_t aTarget, int32_t aValue)
    {
        return std::max(min, std::min(max, aTarget + aValue));
    }

    static int32_t TouchX;
    static int32_t TouchY;
    
    static const uint8_t CursorImage[16*16] =
    {
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 2, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
        0, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0
    };

    unsigned Devices[2] = {RETRO_DEVICE_JOYPAD, RETRO_DEVICE_MOUSE};
}

SoundInterface_struct* SNDCoreList[] =
{
	&SNDDummy,
	&AUDIO::SNDRetro,
	NULL
};

GPU3DInterface* core3DList[] =
{
	&gpu3DRasterize,
	NULL
};



//

void *retro_get_memory_data(unsigned type)
{
   return 0;
}

size_t retro_get_memory_size(unsigned type)
{
   return 0;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_cb = cb;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "DeSmuME";
   info->library_version = "svn";
   info->valid_extensions = "nds";
   info->need_fullpath = true;   
   info->block_extract = false;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
    if(in_port < 2)
    {
        INPUT::Devices[in_port] = device;
    }
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    // TODO
    info->geometry.base_width = 256;
    info->geometry.base_height = 192*2;
    info->geometry.max_width = 256;
    info->geometry.max_height = 192*2;
    info->geometry.aspect_ratio = 256.0 / (192.0 * 2.0);
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_init (void)
{
    struct NDS_fw_config_data fw_config;
    NDS_FillDefaultFirmwareConfigData(&fw_config);

    CommonSettings.num_cores = 2;
    CommonSettings.use_jit = true;

    addonsChangePak(NDS_ADDON_NONE);
    NDS_Init();
    NDS_CreateDummyFirmware(&fw_config);
    NDS_3D_ChangeCore(0);
    SPU_ChangeSoundCore(AUDIO::SNDCORE_RETRO, 735 * 2);
    backup_setManualBackupType(MC_TYPE_AUTODETECT);
}

void retro_deinit(void)
{
	NDS_DeInit();
}

void retro_reset (void)
{
    NDS_Reset();
}

void retro_run (void)
{
    poll_cb();

    // TOUCH: Todo: Support analog to control touch
    if(INPUT::Devices[1] == RETRO_DEVICE_MOUSE)
    {
        INPUT::TouchX = INPUT::ClampedMove<0, 255>(INPUT::TouchX, input_cb(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X));
        INPUT::TouchY = INPUT::ClampedMove<0, 191>(INPUT::TouchY, input_cb(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y));
        
        if(input_cb(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
        {
            NDS_setTouchPos(INPUT::TouchX, INPUT::TouchY);
        }
        else
        {
            NDS_releaseTouch();
        }
    }

    // BUTTONS
    if(INPUT::Devices[0] == RETRO_DEVICE_JOYPAD)
    {
        NDS_beginProcessingInput();
        UserButtons& input = NDS_getProcessingUserInput().buttons;
        input.G = 0; // debug
        input.E = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R); // right shoulder
        input.W = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L); // left shoulder
        input.X = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
        input.Y = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
        input.A = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        input.B = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        input.S = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
        input.T = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
        input.U = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
        input.D = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
        input.L = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
        input.R = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        input.F = 0; //Lid
        NDS_endProcessingInput();
    }

    // AUDIO: Reset frame count
    AUDIO::frames = 0;

    // RUN
    NDS_exec<false>();
    SPU_Emulate_user();
    
    // VIDEO: Swap screen colors and pass on
    VIDEO::SwapScreen<SWAPTYPE>(screenSwap, GPU_screen);
    
    // Draw pointer
    if(INPUT::Devices[1] == RETRO_DEVICE_MOUSE)
    {
        for(int i = 0; i != 16 && INPUT::TouchY + i < 192; i ++)
        {
            for(int j = 0; j != 16 && INPUT::TouchX + j < 256; j ++)
            {
                if(INPUT::CursorImage[i * 16 + j])
                {
                    screenSwap[(256 * (192 + i + INPUT::TouchY)) + (j + INPUT::TouchX)] = 0x7FFF;
                }
            }
        }
    }
    
    video_cb(screenSwap, 256, 192 * 2, 256 * 2);
}

size_t retro_serialize_size (void)
{
    // HACK: Usually around 10 MB but can vary frame to frame!
    return 1024 * 1024 * 12;
}

bool retro_serialize(void *data, size_t size)
{
    EMUFILE_MEMORY state;
    savestate_save(&state, 0);
    
    if(state.size() <= size)
    {
        memcpy(data, state.buf(), state.size());
        return true;
    }
    
    return false;
}

bool retro_unserialize(const void * data, size_t size)
{
    EMUFILE_MEMORY state(const_cast<void*>(data), size);
    return savestate_load(&state);
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned unused, bool unused1, const char* unused2)
{}

bool retro_load_game(const struct retro_game_info *game)
{
    execute = NDS_LoadROM(game->path);
    return execute;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    if(RETRO_GAME_TYPE_SUPER_GAME_BOY == game_type && 2 == num_info)
    {
        strncpy(GBAgameName, info[1].path, sizeof(GBAgameName));
        addonsChangePak(NDS_ADDON_GBAGAME);
        
        return retro_load_game(&info[0]);
    }
    return false;
}

void retro_unload_game (void)
{
    NDS_FreeROM();
    execute = false;
}

unsigned retro_get_region (void)
{ 
   return RETRO_REGION_NTSC; 
}
