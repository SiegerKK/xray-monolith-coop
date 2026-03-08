#pragma once

#include "game_sv_single.h"

class game_sv_Coop : public game_sv_Single
{
    typedef game_sv_Single inherited;

public:
    game_sv_Coop();

    virtual LPCSTR type_name() const { return "coop"; }
    virtual BOOL CanHaveFriendlyFire() { return FALSE; }
};