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

#include <string>
#include "../third_party/json.hpp"

namespace lucid {

// Compile `source` and return the full result document (never throws for
// ordinary program errors — those live in the "errors" array).
nlohmann::json compile(const std::string& source);

// compile() serialised to JSON text, with the serialisation also done on the
// large stack (the JSON writer is recursive too). Used by the server.
std::string compileToJsonText(const std::string& source);

} // namespace lucid
