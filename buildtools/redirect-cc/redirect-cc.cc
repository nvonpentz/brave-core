/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

int main(int argc, char* argv[]) {
  auto env = base::Environment::Create();

  std::string executable = argv[1];
  int start_idx = 2;
  if (env->GetVar("CC_WRAPPER", &executable)) {
    start_idx = 1;
  }

  base::FilePath cur_dir;
  CHECK(base::GetCurrentDirectory(&cur_dir));

  auto launch_cmd_line = base::CommandLine(base::FilePath(executable));
  std::string src_include_arg;

  for (int arg_idx = start_idx; arg_idx < argc; ++arg_idx) {
    const auto& arg = base::StringPiece(argv[arg_idx]);
    if (src_include_arg.empty() && base::StartsWith(arg, "-I") &&
        base::EndsWith(arg, "brave/chromium_src")) {
      src_include_arg = base::StrCat({arg, "/../../.."});
    } else if (arg == "-c") {
      const auto& path_cc = base::StringPiece(argv[arg_idx + 1]);
      base::FilePath abs_path_cc =
          base::MakeAbsoluteFilePath(base::FilePath(path_cc));
      base::FilePath chromium_original_dir =
          base::MakeAbsoluteFilePath(cur_dir.Append("..").Append(".."));
      std::string cr_orig_dir = chromium_original_dir.AsUTF8Unsafe();
      std::string rel_path =  abs_path_cc.AsUTF8Unsafe().substr(cr_orig_dir.size() + 1);

      const char* kOutDirNames[] = {"out", "out_x86"};
      const auto& rel_path_parts = base::SplitStringPiece(
          rel_path, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
      for (const auto* out_dir_name : kOutDirNames) {
        if (rel_path_parts[0] != out_dir_name)
          continue;
        if (rel_path_parts[2] == "gen") {
          rel_path = base::JoinString(
              base::make_span(rel_path_parts.begin() + 3, rel_path_parts.end()),
              "/");
        } else if (rel_path_parts[3] == "gen") {
          rel_path = base::JoinString(
              base::make_span(rel_path_parts.begin() + 4, rel_path_parts.end()),
              "/");
        }
      }
      base::FilePath brave_path = base::FilePath("../../brave/chromium_src")
              .Append(base::FilePath(rel_path));
      if (base::PathExists(brave_path)) {
        launch_cmd_line.AppendArg(arg);
        launch_cmd_line.AppendArg(brave_path.AsUTF8Unsafe());
        ++arg_idx;
        continue;
      }
    }
    launch_cmd_line.AppendArg(arg);
  }
  if (!src_include_arg.empty()) {
    launch_cmd_line.AppendArg(src_include_arg);
  }

  base::LaunchOptions options;
  options.wait = true;

  auto process = base::LaunchProcess(launch_cmd_line, options);

  int exit_code = -1;
  process.WaitForExit(&exit_code);
  return exit_code;
}
