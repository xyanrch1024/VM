#pragma once
#include "chunk.hpp"
#include <string>
#include <vector>

std::string disassembleToText(const Function& func);
std::string disassembleProgramToText(const std::vector<Function*>& funcs);
bool assembleFromText(const std::string& text, std::vector<Function*>& outFuncs, std::string& outError);
