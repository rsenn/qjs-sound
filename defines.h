#ifndef DEFINES_H
#define DEFINES_H

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = { \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} \
    } \
  }

#endif /* defined(DEFINES_H) */
