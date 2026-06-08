// The single translation unit that compiles the stb implementations.
// Every other TU just includes the headers for the declarations.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
