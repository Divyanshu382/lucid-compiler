// ============================================================================
// compiler.hpp — Phase orchestrator for Lucid.
// Input : source text
// Output: one JSON object describing every phase:
//   { tokens, ast, symbolTable,
//     optimized: { astBefore, astAfter, nodesBefore, nodesAfter },
//     ir: { code, quads, temps, labels },
//     timings: { lexer, parser, semantic, optimizer, ircode, interpreter, total },
//     output, errors, success }
//
// Runs Lexer -> Parser -> SemanticAnalyzer -> Optimizer -> IRGenerator ->
// Interpreter in order, timing each phase in microseconds. If a phase reports errors, the later phases are skipped but whatever
// was produced so far is still returned.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include "../third_party/json.hpp"

namespace lucid {

// Compile `source` and return the full result document (never throws for
// ordinary program errors — those live in the "errors" array).
// `input` is the program's standard input, read by cin. When it runs out,
// `moreInput` (optional) supplies more, a line at a time; false at the end.
nlohmann::json compile(const std::string& source, const std::string& input = "",
                       const std::function<bool(std::string&)>& moreInput = nullptr);

// compile() serialised to JSON text, with the serialisation also done on the
// large stack (the JSON writer is recursive too). Used by the server.
std::string compileToJsonText(const std::string& source, const std::string& input = "");

} // namespace lucid
