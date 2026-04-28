#pragma once
#ifndef IMPORTER
#define IMPORTER

#include "baseobject.h"
#include <string>

namespace PEngine
{
    struct Importer
    {
        static BaseObject* ImportFromOBJ(const std::string& filePath);
    };
}

#endif