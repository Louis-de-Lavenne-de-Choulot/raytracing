#pragma once
#include "IScript.h"
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace HonHengine {
    enum class ScriptVarType { Int, Float, Bool, String, ObjectRef };
    // Plain struct instead of union: a union whose members have non-trivial
    // constructors/destructors (std::string in objRef) causes MSVC to delete
    // the destructor of the enclosing type, breaking std::vector<ScriptVariable>.
    struct ScriptVarValue {
        int         i = 0;
        float       f = 0.f;
        bool        b = false;
        char* s = nullptr;   // heap-allocated, caller manages lifetime
        std::string objRef;             // GUID for ObjectRef variables
    };
    struct ScriptVariable { std::string name; ScriptVarType type; ScriptVarValue value; bool dirty = false; };
    struct ScriptComponent {
        std::string scriptGUID; std::string compiledPath; IScript* instance = nullptr;
#ifdef _WIN32
        HMODULE libHandle = nullptr;
#else
        void* libHandle = nullptr;
#endif
        IScript* (*createScript)() = nullptr; void (*destroyScript)(IScript*) = nullptr;
        std::vector<ScriptVariable> exposedVars;
    };
}