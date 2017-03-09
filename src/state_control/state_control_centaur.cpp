
#include <stdlib.h>
#include <stdio.h>

#include "../core/system.h"
#include "../core/console.h"
#include "../core/vmath.h"

#include "../render/camera.h"
#include "../script/script.h"
#include "../vt/tr_versions.h"
#include "../engine.h"
#include "../audio.h"
#include "../controls.h"
#include "../physics.h"
#include "../room.h"
#include "../world.h"
#include "../skeletal_model.h"
#include "../entity.h"
#include "../character_controller.h"
#include "state_control_centaur.h"
#include "state_control.h"


void StateControl_CentaurSetIdleAnim(struct entity_s *ent, int anim_type, int move_type)
{
    switch(move_type)
    {
        case MOVE_ON_FLOOR:
            Entity_SetAnimation(ent, anim_type, TR_ANIMATION_CENTAUR_STAY, 0, NULL);
            break;
    }
}


int StateControl_Centaur(struct entity_s *ent, struct ss_animation_s *ss_anim)
{
    character_command_p cmd = &ent->character->cmd;
    character_state_p state = &ent->character->state;
    uint16_t current_state = Anim_GetCurrentState(ss_anim);
    float *pos = ent->transform + 12;

    ent->character->rotate_speed_mult = 1.0f;
    ss_anim->anim_frame_flags = ANIM_NORMAL_CONTROL;

    state->sprint = 0x00;
    state->crouch = 0x00;

    switch(current_state)
    {
        case TR_STATE_CENTAUR_STAY: // -> 3 -> 4 -> 6
            if(state->dead)
            {
                Entity_SetAnimation(ent, ANIM_TYPE_BASE, TR_ANIMATION_CENTAUR_DEAD, 0, NULL);
            }
            else if(cmd->action)
            {
                ss_anim->next_state = TR_STATE_CENTAUR_AIM;
            }
            else if(cmd->move[0] < 0)
            {
                ss_anim->next_state = TR_STATE_CENTAUR_ARRRR;
            }
            else if(cmd->move[0] > 0)
            {
                ss_anim->next_state = TR_STATE_CENTAUR_RUN;
            }
            else
            {
                ss_anim->next_state = TR_STATE_CENTAUR_STAY;
            }
            break;

        case TR_STATE_CENTAUR_RUN: // -> 1
            ent->dir_flag = ENT_MOVE_FORWARD;
            if(!state->dead && (cmd->move[0] > 0))
            {
                ss_anim->next_state = TR_STATE_CENTAUR_RUN;
            }
            else
            {
                ss_anim->next_state = TR_STATE_CENTAUR_STAY;
            }
            break;

        case TR_STATE_CENTAUR_AIM: // -> 1 -> 2
            if(!state->dead && cmd->action)
            {
                ss_anim->next_state = TR_STATE_CENTAUR_SHOOT;
            }
            else
            {
                ss_anim->next_state = TR_STATE_CENTAUR_STAY;
            }
            break;
    };

    return 0;
}
