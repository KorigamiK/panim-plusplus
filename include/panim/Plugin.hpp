// Plugin export helper. Use PANIM_EXPORT_ANIMATION(MyAnimation) once per plugin.
#pragma once

#if defined(_WIN32)
#define PANIM_PLUGIN_EXPORT __declspec(dllexport)
#else
#define PANIM_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define PANIM_EXPORT_ANIMATION(AnimationType)                                                                                                        \
    extern "C" PANIM_PLUGIN_EXPORT uint32_t panim_plugin_api_version() { return panim::plugin_api_version; }                                         \
    extern "C" PANIM_PLUGIN_EXPORT panim::Animation *create_animation() { return new AnimationType(); }                                              \
    extern "C" PANIM_PLUGIN_EXPORT void destroy_animation(panim::Animation *animation) { delete animation; }
