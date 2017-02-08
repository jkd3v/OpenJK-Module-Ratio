#include "cgame/cg_local.h"
#include "mppShared.h"

static MultiPlugin_t	*sys;
static MultiSystem_t	*trap;

static vmCvar_t cvar_ratioDraw;
static vmCvar_t cvar_ratioPosX;
static vmCvar_t cvar_ratioPosY;
static vmCvar_t cvar_ratioSize;
static vmCvar_t cvar_ratioStats;

static int savedTime = -1;
static int currentTeam = TEAM_SPECTATOR;
static unsigned int nbSuicides = 0;
static qboolean isDead[MAX_CLIENTS];
static struct {
	int killedMe;
	int killedByMe;
} playerStats[MAX_CLIENTS];

static void resetRatio() {
	nbSuicides = 0;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		playerStats[i].killedMe = 0;
		playerStats[i].killedByMe = 0;
		isDead[i] = qfalse;
	}
}


static float calculateRatio(int kills, int deaths) {
	if (kills < 1)
	{
		return calculateRatio(1, (deaths - kills) + 1);
	}
	if (deaths < 1)
	{
		return ((float)kills + ((-1.f)*(float)deaths) + 1.f);
	}
	else
	{
		return (float)kills / (float)deaths;
	}
}

/**************************************************
* mpp
*
* Plugin exported function. This function gets called
* the moment the module is loaded and provides a
* pointer to a shared structure. Store the pointer
* and copy the System pointer safely.
**************************************************/

__declspec(dllexport) void mpp(MultiPlugin_t *pPlugin)
{
	sys = pPlugin;
	trap = sys->System;

	trap->Cvar.Register(&cvar_ratioDraw, "ratio_draw", "1", CVAR_ARCHIVE);
	trap->Cvar.Register(&cvar_ratioPosX, "ratio_positionX", "638", CVAR_ARCHIVE);
	trap->Cvar.Register(&cvar_ratioPosY, "ratio_positionY", "150", CVAR_ARCHIVE);
	trap->Cvar.Register(&cvar_ratioSize, "ratio_size", "0.7", CVAR_ARCHIVE);
	trap->Cvar.Register(&cvar_ratioStats, "ratio_drawStats", "1", CVAR_ARCHIVE);

	resetRatio();
}

/**************************************************
* mppPreSystem
*
* Plugin exported function. This function gets called
* from the game module to perform an action in the
* engine, before going into the engine.
**************************************************/

__declspec(dllexport) int mppPostMain(int cmd, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11)
{
	switch (cmd)
	{
		case CG_DRAW_ACTIVE_FRAME:
		{
			if (cvar_ratioDraw.integer != 0) {
				// Draw the ratio
				int diff = sys->snap->ps.persistant[PERS_SCORE] - sys->snap->ps.persistant[PERS_KILLED] + nbSuicides;
				float ratio = calculateRatio(sys->snap->ps.persistant[PERS_SCORE], sys->snap->ps.persistant[PERS_KILLED] - nbSuicides);
				sys->mppRawTextCalculateDraw(sys->va("Ratio: %.2f (^%c%s%i^7)", ratio, (diff < 0 ? '1':(diff>0)?'2':'3'), (diff>=0?"+":""), diff), cvar_ratioPosX.integer, cvar_ratioPosY.integer, cvar_ratioSize.value, 1, 0, TopRight);
			}
			if (currentTeam != TEAM_SPECTATOR && cvar_ratioStats.integer != 0) {
				int trickedIndex;
				if (sys->cg->clientNum > 47) trickedIndex = 3;
				if (sys->cg->clientNum > 31) trickedIndex = 2;
				if (sys->cg->clientNum > 15) trickedIndex = 1;
				else trickedIndex = 0;
				// Draw the stats for each players
				for (int i = 0; i < sys->cgs->maxclients; i++) {
					if (i == sys->cg->clientNum || sys->mppIsPlayerAlly(i)) continue;
					centity_t *cent = sys->mppIsPlayerEntity(i);
					if (cent && !(cent->currentState.eFlags & EF_DEAD)) {
						int *trick = &cent->currentState.trickedentindex;
						trick += trickedIndex;
						if (!(*trick & (1 << (sys->cg->clientNum % 16))))
						{
							// Center the text
							char	stats[15],
								statsSpaces[MAX_NAME_LENGTH],
								nameSpaces[MAX_NAME_LENGTH];
							int		spacesDiff = ((int)(sys->Com_Sprintf(stats, 15, "^2%i^7/^1%i", playerStats[i].killedByMe, playerStats[i].killedMe) - 6 - strlen(sys->clientInfo[i].name))) / 2;
							int j;
							for (j = 0; j < -spacesDiff && j < MAX_NAME_LENGTH; j++) {
								statsSpaces[j] = ' ';
							}
							statsSpaces[j] = '\0';
							for (j = 0; j < spacesDiff && j < MAX_NAME_LENGTH; j++) {
								nameSpaces[j] = ' ';
							}
							nameSpaces[j] = '\0';

							sys->mppRenderTextAtEntity(i, sys->va("%s%s\n%s%s", nameSpaces, sys->clientInfo[i].name, statsSpaces, stats), qtrue, qfalse, MiddleCenter);
						}
					}
				}
			}
			break;
		}
		default:
			break;
	}

	return sys->noBreakCode;
}
__declspec(dllexport) int mppPostSystem(int *args) {
	switch (args[0])
	{
		case CG_GETGAMESTATE:
		{ // The server inform us something changed
			
			if (currentTeam != sys->clientInfo[sys->cg->clientNum].team) {
				// We changed team
				resetRatio();
				currentTeam = sys->clientInfo[sys->cg->clientNum].team;
			}
			if (savedTime == -1) savedTime = sys->cg->time - sys->cgs->levelStartTime;
			else if (savedTime > sys->cg->time - sys->cgs->levelStartTime) {
				// Map restarted
				resetRatio();
			}
			// Update the last known time
			savedTime = sys->cg->time - sys->cgs->levelStartTime;

			break;
		}
		case CG_GETSNAPSHOT:
		{
			qboolean stillDead[MAX_CLIENTS];
			memset(stillDead, qfalse, MAX_CLIENTS * sizeof(qboolean));

			// I died :(
			for (int i = 0; i < sys->snap->numEntities; i++) {
				entityState_t *state = &sys->snap->entities[i];
				if (state->eType == EV_OBITUARY + ET_EVENTS) {
					if (isDead[state->otherEntityNum]) {
						// Target already dead
						stillDead[state->otherEntityNum] = qtrue;
						continue;
					}
					// Someone died
					if (state->otherEntityNum == sys->snap->ps.clientNum) {
						if (
							state->otherEntityNum2 == sys->snap->ps.clientNum ||
							state->otherEntityNum2 < 0 ||
							state->otherEntityNum2 >= sys->cgs->maxclients
							) {
							// Its a suicide
							nbSuicides++;
						}
						else {
							// It's a kill
							playerStats[state->otherEntityNum2].killedMe++;
						}
					}
					else if (state->otherEntityNum2 == sys->snap->ps.clientNum) {
						playerStats[state->otherEntityNum].killedByMe++;
					}
					isDead[state->otherEntityNum] = qtrue;
					stillDead[state->otherEntityNum] = qtrue;
				}
				else if (state->eType == EV_CLIENTJOIN + ET_EVENTS) {
					// Player joined, reinit him
					playerStats[state->eventParm].killedMe = 0;
					playerStats[state->eventParm].killedByMe = 0;
				}
			}
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (isDead[i] && !stillDead[i]) isDead[i] = qfalse;
			}
			break;
		}
		case CG_CVAR_SET:
		case CG_CVAR_UPDATE:
		{
			trap->Cvar.Update(&cvar_ratioDraw);
			trap->Cvar.Update(&cvar_ratioPosX);
			trap->Cvar.Update(&cvar_ratioPosY);
			trap->Cvar.Update(&cvar_ratioSize);
			trap->Cvar.Update(&cvar_ratioStats);
			break;
		}
		default:
			break;
	}

	return sys->noBreakCode;
}