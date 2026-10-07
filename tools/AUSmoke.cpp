#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
void check(OSStatus status, const char* operation)
{
    if (status != noErr)
    {
        std::fprintf(stderr, "%s failed: %d\n", operation, static_cast<int>(status));
        std::exit(1);
    }
}
OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
               UInt32 bus, UInt32 count, AudioBufferList* buffers)
{
    for (UInt32 c = 0; c < buffers->mNumberBuffers; ++c)
        if (auto* data = static_cast<float*>(buffers->mBuffers[c].mData))
            for (UInt32 i = 0; i < count; ++i)
                data[i] = static_cast<float>(0.4 * std::sin((time->mSampleTime + i) * 0.05 * (1 + bus + c)));
    return noErr;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(argv[1]),
                                                       static_cast<CFIndex>(std::strlen(argv[1])), true);
    auto bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle))
    { std::fputs("Could not load the AU bundle\n", stderr); return 3; }
    auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("DistInterleaveAUFactory")));
    if (!factory) return 4;
    AudioComponentDescription description {kAudioUnitType_Effect, 'Ditl', 'VSil', 0, 0};
    auto component = AudioComponentRegister(&description, CFSTR("DistInterleave test"), 0x100, factory);
    if (!component)
    {
        std::fputs("AudioComponentRegister failed (macOS service access may be sandboxed)\n", stderr);
        return 5;
    }
    for (UInt32 main : {1u, 2u})
        for (UInt32 side : {1u, 2u})
            for (UInt32 output : {1u, 2u})
            {
                AudioUnit unit = nullptr;
                check(AudioComponentInstanceNew(component, &unit), "Instantiate");
                for (UInt32 bus : {0u, 1u, 2u})
                {
                    const auto channels = bus == 0 ? main : (bus == 1 ? side : output);
                    const auto scope = bus == 2 ? kAudioUnitScope_Output : kAudioUnitScope_Input;
                    AudioStreamBasicDescription format {48000, kAudioFormatLinearPCM,
                        kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
                        sizeof(float), 1, sizeof(float), channels, 32, 0};
                    check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, scope,
                          bus == 2 ? 0 : bus, &format, sizeof(format)), "Set bus format");
                }
                AURenderCallbackStruct callback {input, nullptr};
                for (UInt32 bus : {0u, 1u})
                    check(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                          kAudioUnitScope_Input, bus, &callback, sizeof(callback)), "Set input callback");
                check(AudioUnitInitialize(unit), "Initialize");
                UInt32 size = 0;
                check(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global,
                      0, &size, nullptr), "Parameter list");
                if (size != 5 * sizeof(AudioUnitParameterID)) return 6;
                std::array<AudioUnitParameterID, 5> ids {};
                check(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global,
                      0, ids.data(), &size), "Read parameter IDs");
                for (auto id : ids)
                {
                    AudioUnitParameterInfo info {};
                    UInt32 infoSize = sizeof(info);
                    check(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                          id, &info, &infoSize), "Read parameter info");
                    if (std::strcmp(info.name, "Mode") == 0)
                        check(AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, 1, 0), "Select sidechain");
                    if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString)
                        CFRelease(info.cfNameString);
                }
                std::array<std::array<float, 128>, 2> samples {};
                struct Buffers { UInt32 count; AudioBuffer buffers[2]; } buffers {output, {}};
                for (UInt32 c = 0; c < output; ++c)
                    buffers.buffers[c] = {1, sizeof(samples[c]), samples[c].data()};
                double energy = 0;
                for (int block = 0; block < 32; ++block)
                {
                    AudioTimeStamp time {};
                    time.mSampleTime = block * 128;
                    time.mFlags = kAudioTimeStampSampleTimeValid;
                    AudioUnitRenderActionFlags flags = 0;
                    check(AudioUnitRender(unit, &flags, &time, 0, 128,
                          reinterpret_cast<AudioBufferList*>(&buffers)), "Render");
                    for (UInt32 c = 0; c < output; ++c)
                        for (float sample : samples[c])
                        {
                            if (!std::isfinite(sample)) return 7;
                            energy += sample * sample;
                        }
                }
                if (energy < 1.0) return 8;
                check(AudioUnitUninitialize(unit), "Uninitialize");
                check(AudioComponentInstanceDispose(unit), "Dispose");
            }
    std::puts("AU loaded and rendered all 8 enabled-sidechain mono/stereo layouts");
    CFRelease(bundle);
}
