/*
 * Copyright 2017 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// wasm-emscripten-finalize console tool
// Performs Emscripten-specific transforms on .wasm files
//

#include <exception>

#include "ir/trapping.h"
#include "support/colors.h"
#include "support/command-line.h"
#include "support/file.h"
#include "wasm-binary.h"
#include "wasm-emscripten.h"
#include "wasm-io.h"
#include "wasm-linker.h"
#include "wasm-printing.h"
#include "wasm-validator.h"

using namespace cashew;
using namespace wasm;

int main(int argc, const char *argv[]) {
  std::string infile;
  std::string outfile;
  bool emitBinary = true;
  std::vector<Name> forcedExports;
  Options options("wasm-emscripten-finalize",
                  "Performs Emscripten-specific transforms on .wasm files");
  options
      .add("--output", "-o", "Output file",
           Options::Arguments::One,
           [&outfile](Options*, const std::string &argument) {
             outfile = argument;
             Colors::disable();
           })
      .add("--emit-text", "-S", "Emit text instead of binary for the output file",
           Options::Arguments::Zero,
           [&emitBinary](Options*, const std::string &) {
             emitBinary = false;
           })
      .add("--force-exports", "", "Comma-separated list of functions to export",
           Options::Arguments::One,
           [&forcedExports](Options *o, const std::string &argument) {
              std::string name = "";
              for (unsigned i = 0; i < argument.size(); ++i) {
                char c = argument[i];
                if (c == ',') {
                  forcedExports.push_back(name);
                  name = "";
                } else {
                  name += c;
                }
              }
              if (name != "") {
                forcedExports.push_back(name);
              }
           })
      .add_positional("INFILE", Options::Arguments::One,
                      [&infile](Options *o, const std::string &argument) {
                        infile = argument;
                      });
  options.parse(argc, argv);

  if (infile == "") {
    Fatal() << "Need to specify an infile\n";
  }
  if (outfile == "" && emitBinary) {
    Fatal() << "Need to specify an outfile, or use text output\n";
  }

  Module wasm;
  ModuleReader reader;
  reader.read(infile, wasm);

  if (options.debug) {
    std::cerr << "Module before:\n";
    WasmPrinter::printModule(&wasm, std::cerr);
  }

  // for (Name name : forcedExports) {
  // TODO: only export initializer functions and maybe certain Emscripten-expected
  // functions. Today lld assumes linking is done and only exports main by
  // default, dynamic linking support should allow other symbols to be exported
  // according to C visibility, which is less than all functions. For now this works.
  for (auto &function : wasm.functions) {
    auto name = function->name;
    if (wasm.getExportOrNull(name)) {
      continue;
    }
    auto exported = make_unique<Export>();
    exported->name = name;
    exported->value = name;
    exported->kind = ExternalKind::Function;
    wasm.addExport(exported.release());
  }

  EmscriptenGlueGenerator generator(wasm);
  generator.generateRuntimeFunctions();
  generator.generateMemoryGrowthFunction();
  generator.generateDynCallThunks();
  generator.fixEmAsmConsts();

  if (options.debug) {
    std::cerr << "Module after:\n";
    WasmPrinter::printModule(&wasm, std::cerr);
  }

  ModuleWriter writer;
  // writer.setDebug(options.debug);
  writer.setDebugInfo(true);
  // writer.setDebugInfo(options.passOptions.debugInfo);
  // writer.setSymbolMap(symbolMap);
  writer.setBinary(emitBinary);
  // if (emitBinary) {
  //   writer.setSourceMapFilename(sourceMapFilename);
  //   writer.setSourceMapUrl(sourceMapUrl);
  // }
  writer.write(wasm, outfile);

  return 0;
}