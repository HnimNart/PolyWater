
/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 */

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>
#include <string>
#include <vector>

#include "file_dialog.hpp"

namespace app {

/**
 * Helper to convert C++ filter strings like:
 * "GLTF|*.gltf;*.glb|All Files|*.*" or just "*"
 * into a macOS NSArray of extensions.
 */
/**********************************************************/
static NSArray<NSString *> *ParseExtensions(const char *exts)
/**********************************************************/
{
  if (!exts || strlen(exts) == 0 || strcmp(exts, "*") == 0 ||
      strcmp(exts, "*.*") == 0) {
    return nil;
  }

  NSMutableArray<NSString *> *array = [NSMutableArray array];
  std::string s(exts);
  size_t start = 0;

  // Basic parsing logic for "Name|*.ext;*.ext2|..."
  while (start < s.size()) {
    size_t sep = s.find('|', start);
    if (sep == std::string::npos)
      break;

    // Jump past the name (e.g., "GLTF|")
    start = sep + 1;
    sep = s.find('|', start);
    std::string spec = (sep == std::string::npos)
                           ? s.substr(start)
                           : s.substr(start, sep - start);

    // Process the spec which might be "*.gltf;*.glb"
    size_t spec_start = 0;
    while (spec_start < spec.size()) {
      size_t semi = spec.find(';', spec_start);
      std::string ext = (semi == std::string::npos)
                            ? spec.substr(spec_start)
                            : spec.substr(spec_start, semi - spec_start);

      // Clean "*.ext" to "ext"
      if (ext.starts_with("*."))
        ext = ext.substr(2);
      if (ext != "*" && ext != "*.*") {
        [array addObject:[NSString stringWithUTF8String:ext.c_str()]];
      }

      if (semi == std::string::npos)
        break;
      spec_start = semi + 1;
    }

    if (sep == std::string::npos)
      break;
    start = sep + 1;
  }
  return array.count > 0 ? array : nil;
}

static std::filesystem::path
/**********************************************************/
RunDialog(bool isSave, bool isFolder, const char *title, const char *exts,
          const std::filesystem::path *initialDir = nullptr)
/**********************************************************/
{
  @autoreleasepool {
    NSSavePanel *panel = nil;
    if (isSave) {
      panel = [NSSavePanel savePanel];
    } else {
      NSOpenPanel *openPanel = [NSOpenPanel openPanel];
      [openPanel setCanChooseFiles:!isFolder];
      [openPanel setCanChooseDirectories:isFolder];
      [openPanel setAllowsMultipleSelection:NO];
      panel = openPanel;
    }

    if (title) {
      [panel setTitle:[NSString stringWithUTF8String:title]];
      [panel setMessage:[NSString stringWithUTF8String:title]];
    }

    if (initialDir && !initialDir->empty() &&
        std::filesystem::exists(*initialDir)) {
      NSString *pathStr =
          [NSString stringWithUTF8String:initialDir->string().c_str()];
      [panel setDirectoryURL:[NSURL fileURLWithPath:pathStr isDirectory:YES]];
    }

    NSArray<NSString *> *allowedExtensions = ParseExtensions(exts);
    if (allowedExtensions) {
      if (@available(macOS 11.0, *)) {
        NSMutableArray<UTType *> *contentTypes = [NSMutableArray array];
        for (NSString *ext in allowedExtensions) {
          UTType *type = [UTType typeWithFilenameExtension:ext];
          if (type) {
            [contentTypes addObject:type];
          }
        }
        [panel setAllowedContentTypes:contentTypes];
      } else {
        // Fallback on earlier versions
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [panel setAllowedFileTypes:allowedExtensions];
#pragma clang diagnostic pop
      }
    }

    if ([panel runModal] == NSModalResponseOK) {
      return std::filesystem::path([[[panel URL] path] UTF8String]);
    }
  }
  return {};
}

/**********************************************************/
std::filesystem::path windowOpenFileDialog(struct GLFWwindow *glfwin,
                                           const char *title,
                                           const char *exts)
/**********************************************************/
{
  return RunDialog(false, false, title, exts);
}

/**********************************************************/
std::filesystem::path windowOpenFileDialog(struct GLFWwindow *glfwin,
                                           const char *title, const char *exts,
                                           std::filesystem::path &initialDir)
/**********************************************************/
{
  std::filesystem::path result =
      RunDialog(false, false, title, exts, &initialDir);
  if (!result.empty()) {
    initialDir = result.parent_path();
  }
  return result;
}

/**********************************************************/
std::filesystem::path windowSaveFileDialog(struct GLFWwindow *glfwin,
                                           const char *title,
                                           const char *exts)
/**********************************************************/
{
  return RunDialog(true, false, title, exts);
}

/**********************************************************/
std::filesystem::path windowOpenFolderDialog(struct GLFWwindow *glfwin,
                                             const char *title)
/**********************************************************/
{
  return RunDialog(false, true, title, nullptr);
}

} // namespace app

#endif // __APPLE__
