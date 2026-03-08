#pragma once

#include "game_cl_single.h"

class game_cl_Coop : public game_cl_Single
{
    typedef game_cl_Single inherited;

public:
    game_cl_Coop();
    virtual LPCSTR type_name() const { return "coop"; }
};
