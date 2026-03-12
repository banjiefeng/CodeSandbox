#pragma once

#include <string>

class SeccompManager
{
public:
    bool apply(std::string& error) const;
};
