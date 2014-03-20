// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"

#import <Cocoa/Cocoa.h>
#include <copyfile.h>

#include "base/file_path.h"
#include "base/logging.h"
#include "base/string_util.h"

namespace file_util {

bool GetTempDir(FilePath* path) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSString* tmp = NSTemporaryDirectory();
  if (tmp == nil) {
    [pool release];
    return false;
  }
  *path = FilePath([tmp fileSystemRepresentation]);
  [pool release];
  return true;
}

bool GetShmemTempDir(FilePath* path) {
  return GetTempDir(path);
}

bool CopyFile(const FilePath& from_path, const FilePath& to_path) {
  return (copyfile(from_path.value().c_str(),
                   to_path.value().c_str(), NULL, COPYFILE_ALL) == 0);
}

}  // namespace
