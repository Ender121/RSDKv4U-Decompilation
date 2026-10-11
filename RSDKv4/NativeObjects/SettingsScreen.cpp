#include "RetroEngine.hpp"

// ---------------------------------------------------------------------------------------------
// Gameplay panel helpers (CD / S2 style, Spin Dash, Super Peel Out and Extended Camera)
// Each row has two buttons: column 0 = CD (ON for Extended Camera), column 1 = S2 (OFF for Extended Camera)
// ---------------------------------------------------------------------------------------------
static int SettingsScreen_GameplayActive(int row)
{
    SaveGame *saveGame = (SaveGame *)saveRAM;
    switch (row) {
        case 0: return saveGame->gameplayCD ? 0 : 1;
        case 1: return saveGame->spindashS2 ? 1 : 0;
        case 2: return saveGame->peelOutS2 ? 1 : 0;
        default: return saveGame->extendedCamOff ? 1 : 0;
    }
}

static void SettingsScreen_UpdateGameplayButtons(NativeEntity_SettingsScreen *self)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            NativeEntity_PushButton *b = self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_CD + r * 2 + c];
            b->bgColor                 = (SettingsScreen_GameplayActive(r) == c) ? 0x00A048 : 0x006020;
            b->bgColorSelected         = 0x00C060;
        }
    }
}

static void SettingsScreen_SetGameplay(NativeEntity_SettingsScreen *self, int row, int col)
{
    SaveGame *saveGame = (SaveGame *)saveRAM;
    bool changed       = false;
    switch (row) {
        case 0:
            saveGame->gameplayCD = (col == 0);
            SetGlobalVariableByName("options.originalControls", saveGame->gameplayCD ? 1 : 0);
            SetGlobalVariableByName("options.airSpeedCap", saveGame->gameplayCD ? 1 : 0);
            break;
        case 1:
            changed              = saveGame->spindashS2 != (col == 1);
            saveGame->spindashS2 = (col == 1);
            break;
        case 2:
            changed              = saveGame->peelOutS2 != (col == 1);
            saveGame->peelOutS2 = (col == 1);
            break;
        default: saveGame->extendedCamOff = (col == 1); break;
    }
    SettingsScreen_UpdateGameplayButtons(self);
    WriteSaveRAMData();

    // Spin Dash / Peel Out: restart notice (pause menu options only); the text starts tiny and grows
    if (changed && self->isPauseMenu && self->gpNoteK == 0.0f)
        self->gpNoteK = 0.05f;
}

// shows the Gameplay panel buttons or the main page buttons (the unused ones stay invisible)
static void SettingsScreen_ShowGameplay(NativeEntity_SettingsScreen *self, bool show)
{
    for (int i = 0; i <= SETTINGSSCREEN_BTN_GAMEPLAY; ++i) self->buttons[i]->alpha = show ? 0 : 255;
    if (!show && Engine.gameType != GAME_SONIC1 && Engine.gameType != GAME_SONICCD) {
        self->buttons[SETTINGSSCREEN_BTN_SDON]->alpha  = 0;
        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->alpha = 0;
    }
    if (Engine.gameType != GAME_SONICCD)
        self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->alpha = 0;
    for (int i = SETTINGSSCREEN_BTN_GP_STYLE_CD; i < SETTINGSSCREEN_BTN_COUNT; ++i) self->buttons[i]->alpha = show ? 255 : 0;
    self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_CD]->alpha     = 0;
    self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_S2]->alpha     = 0;
}

// puts the Gameplay button right next to the Controls button using the real button widths
static void SettingsScreen_PlaceGameplayButton(NativeEntity_SettingsScreen *self)
{
    NativeEntity_PushButton *c = self->buttons[SETTINGSSCREEN_BTN_CTRLS];
    NativeEntity_PushButton *g = self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY];
    if (c->textWidth <= 0.0f || g->textWidth <= 0.0f)
        return;
    float cHalf = c->textWidth + c->scale * 64.0f;
    float gHalf = g->textWidth + g->scale * 64.0f;
    g->x        = c->x + cHalf + 10.0f + gHalf;
    float over  = (g->x + gHalf) - 140.0f;
    if (over > 0.0f) {
        c->x -= over;
        g->x -= over;
    }
}

// lines up the Gameplay rows (Spin Dash, Super Peel Out, Extended Camera) with the real button widths
static float gameplayLabelScale = 0.125f;

static void SettingsScreen_PlaceGameplayRows(NativeEntity_SettingsScreen *self)
{
    float maxHalf = 0.0f;
    for (int r = 1; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            NativeEntity_PushButton *b = self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_CD + r * 2 + c];
            if (b->textWidth <= 0.0f)
                return;
            float half = b->textWidth + b->scale * 64.0f;
            if (half > maxHalf)
                maxHalf = half;
        }
    }
    float x2 = 140.0f - maxHalf;
    float x1 = x2 - 2.0f * maxHalf - 8.0f;

    // buttons stay where they are; if the longest label would run under them, shrink the labels a bit
    float labelScale = Engine.language == RETRO_JP ? 0.09f : 0.125f;
    float labelMax   = 0.0f;
    for (int r = 1; r < 4; ++r) {
        float w = GetTextWidth(self->gameplayRowText[r], FONT_LABEL, labelScale);
        if (w > labelMax)
            labelMax = w;
    }
    float avail = (x1 - maxHalf) - 8.0f - (-128.0f); // space between label origin and first button column
    if (labelMax > avail && labelMax > 0.0f && avail > 0.0f)
        labelScale *= avail / labelMax;
    gameplayLabelScale = labelScale;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            NativeEntity_PushButton *b = self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_CD + r * 2 + c];
            b->x                       = c ? x2 : x1;
            b->y                       = r == 0 ? 1000.0f : 62.0f - (r - 1) * 32.0f; // row 0 (STYLE) is removed
        }
    }
}

void SettingsScreen_Create(void *objPtr)
{
    RSDK_THIS(SettingsScreen);

    self->gpNoteK = 0.0f;
    NativeEntity_PushButton *button;
    SaveGame *saveGame = (SaveGame *)saveRAM;

    NativeEntity_TextLabel *label = CREATE_ENTITY(TextLabel);
    self->label                   = label;
    label->useRenderMatrix        = true;
    label->fontID                 = FONT_HEADING;
    label->scale                  = 0.2;
    label->alpha                  = 256;
    label->x                      = -144.0;
    label->y                      = 100.0;
    label->z                      = 16.0;
    label->state                  = TEXTLABEL_STATE_IDLE;
    SetStringToFont(self->label->text, strSettings, FONT_HEADING);
    self->panelMesh = LoadMesh("Data/Game/Models/Panel.bin", -1);
    SetMeshVertexColors(self->panelMesh, 0, 0, 0, 0xC0);
    self->arrowsTex = LoadTexture("Data/Game/Menu/ArrowButtons.png", TEXFMT_RGBA4444);
    SetStringToFont(self->musicText, strMusic, FONT_LABEL);
    SetStringToFont(self->sfxText, strSoundFX, FONT_LABEL);
    if (Engine.gameType == GAME_SONIC1) {
        SetStringToFont(self->spindashText, strSpindash, FONT_LABEL);
    }
#if !RETRO_USE_ORIGINAL_CODE
    else if (Engine.gameType == GAME_SONICCD) {
        SetStringToFont8(self->spindashText, "SOUNDTRACK", FONT_LABEL);
    }
#endif
    SetStringToFont(self->boxArtText, strBoxArt, FONT_LABEL);
    for (int i = 0; i < 4; ++i) {
        button                  = CREATE_ENTITY(PushButton);
        self->buttons[i]        = button;
        button->x               = 32.0f + (i % 2) * 76;
        button->y               = 62.0f - (i / 2) * 32;
        button->z               = 0.0;
        button->scale           = 0.175;
        button->bgColor         = 0x00A048;
        button->bgColorSelected = 0x00C060;
        button->useRenderMatrix = true;
        SetStringToFont8(self->buttons[i]->text, ((i % 2) ? "+" : "-"), FONT_LABEL);
    }

    button                                 = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_SDON] = button;
    button->x                              = 32.0;
    button->y                              = -2.0;
    button->z                              = 0.0;
    button->scale                          = 0.175;
    button->useRenderMatrix                = true;
    button->bgColorSelected                = 0x00C060;
#if !RETRO_USE_ORIGINAL_CODE
    if (Engine.gameType == GAME_SONICCD) {
        button->bgColor = saveGame->soundtrackJP ? 0x00A048 : 0x006020;
        SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_SDON]->text, "JP", FONT_LABEL);
    }
    else
#endif
    {
        button->bgColor = saveGame->spindashEnabled ? 0x00A048 : 0x006020;
        SetStringToFont(self->buttons[SETTINGSSCREEN_BTN_SDON]->text, strOn, FONT_LABEL);
    }

    button                                  = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_SDOFF] = button;
    button->x                               = 96.0;
    button->y                               = -2.0;
    button->z                               = 0.0;
    button->scale                           = 0.175;
    button->useRenderMatrix                 = true;
    button->bgColorSelected                 = 0x00A048;
#if !RETRO_USE_ORIGINAL_CODE
    if (Engine.gameType == GAME_SONICCD) {
        button->bgColor = !saveGame->soundtrackJP ? 0x00A048 : 0x006020;
        SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_SDOFF]->text, "US", FONT_LABEL);
    }
    else
#endif
    {
        button->bgColor = !saveGame->spindashEnabled ? 0x00A048 : 0x006020;
        SetStringToFont(self->buttons[SETTINGSSCREEN_BTN_SDOFF]->text, strOff, FONT_LABEL);
    }
    if (Engine.gameType != GAME_SONIC1
#if !RETRO_USE_ORIGINAL_CODE
        && Engine.gameType != GAME_SONICCD
#endif
    ) {
        self->buttons[SETTINGSSCREEN_BTN_SDON]->alpha  = 0;
        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->alpha = 0;
    }

    button                               = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_JP] = button;
    button->x                            = 4.0;
    button->y                            = -34.0;
    button->z                            = 0.0;
    button->scale                        = 0.175;
    button->useRenderMatrix              = true;
    button->bgColorSelected              = 0x00A048;
    button->bgColor                      = saveGame->boxRegion == REGION_JP ? 0x00A048 : 0x006020;
    SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_JP]->text, "JP", FONT_LABEL);

    button                               = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_US] = button;
    button->x                            = 52.0;
    button->y                            = -34.0;
    button->z                            = 0.0;
    button->scale                        = 0.175;
    button->useRenderMatrix              = true;
    button->bgColorSelected              = 0x00C060;
    button->bgColor                      = saveGame->boxRegion == REGION_US ? 0x00A048 : 0x006020;
    SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_US]->text, "US", FONT_LABEL);

    button                               = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_EU] = button;
    button->x                            = 100.0;
    button->y                            = -34.0;
    button->z                            = 0.0;
    button->scale                        = 0.175;
    button->useRenderMatrix              = true;
    button->bgColorSelected              = 0x00A048;
    button->bgColor                      = saveGame->boxRegion == REGION_EU ? 0x00A048 : 0x006020;
    SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_EU]->text, "EU", FONT_LABEL);

    button                                  = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_CTRLS] = button;
    button->useRenderMatrix                 = true;
    button->x                               = -52.0;
    button->y                               = -64.0;
    button->z                               = 0.0;
    button->scale                           = 0.13;
    button->bgColor                         = 0x00A048;
    button->bgColorSelected                 = 0x00C060;
    SetStringToFont(button->text, strControls, FONT_LABEL);

    // Gameplay button, next to Controls (its x is placed from the real button widths)
    button                                     = CREATE_ENTITY(PushButton);
    self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY] = button;
    button->useRenderMatrix                    = true;
    button->x                                  = 52.0;
    button->y                                  = -64.0;
    button->z                                  = 0.0;
    button->scale                              = 0.13;
    button->bgColor                            = 0x00A048;
    button->bgColorSelected                    = 0x00C060;
    SetStringToFont8(button->text, "GAMEPLAY", FONT_LABEL);
    if (Engine.gameType != GAME_SONICCD)
        button->alpha = 0;

    // Gameplay panel: row labels and the two buttons of each row
    SetStringToFont8(self->gameplayRowText[0], "STYLE", FONT_LABEL);
    SetStringToFont8(self->gameplayRowText[1], "SPIN DASH", FONT_LABEL);
    SetStringToFont8(self->gameplayRowText[2], "SUPER PEEL OUT", FONT_LABEL);
    SetStringToFont8(self->gameplayRowText[3], "EXTENDED CAMERA", FONT_LABEL);
    const char *gpNames[8] = { "CD", "S2", "CD", "S2", "CD", "S2", "ON", "OFF" };
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            int idx                 = SETTINGSSCREEN_BTN_GP_STYLE_CD + r * 2 + c;
            button                  = CREATE_ENTITY(PushButton);
            self->buttons[idx]      = button;
            button->useRenderMatrix = true;
            button->x               = 56.0f + c * 52.0f;
            button->y               = r == 0 ? 1000.0f : 62.0f - (r - 1) * 32.0f;
            button->z               = 0.0;
            button->scale           = 0.175;
            button->alpha           = 0; // hidden until the Gameplay panel is shown
            SetStringToFont8(button->text, gpNames[r * 2 + c], FONT_LABEL);
        }
    }
    SettingsScreen_UpdateGameplayButtons(self);
    self->gpSelected = 1;

    if (Engine.gameDeviceType == RETRO_MOBILE) {
        switch (GetGlobalVariableByName("options.physicalControls")) {
            default: break;
            case CTRLS_MOGA:
                if (timeAttackTex)
                    ReplaceTexture("Data/Game/Menu/Moga.png", timeAttackTex);
                else
                    self->controllerTex = LoadTexture("Data/Game/Menu/Moga.png", TEXFMT_RGBA5551);
                break;
            case CTRLS_MOGAPRO:
                if (timeAttackTex)
                    ReplaceTexture("Data/Game/Menu/MogaPro.png", timeAttackTex);
                else
                    self->controllerTex = LoadTexture("Data/Game/Menu/MogaPro.png", TEXFMT_RGBA5551);
                break;
        }
    }
    else {
        self->controllerTex = LoadTexture("Data/Game/Menu/Generic.png", TEXFMT_RGBA8888);
    }
}

void SettingsScreen_Main(void *objPtr)
{
    RSDK_THIS(SettingsScreen);

    SaveGame *saveGame                    = (SaveGame *)saveRAM;
    NativeEntity_OptionsMenu *optionsMenu = self->optionsMenu;

    switch (self->state) {
        case SETTINGSSCREEN_STATE_ENTER:
            if (self->alpha <= 255)
                self->alpha += 8;

            self->buttonMatScale = fminf(self->buttonMatScale + ((1.05 - self->buttonMatScale) / ((60.0 * Engine.deltaTime) * 8.0)), 1.0f);

            NewRenderState();
            MatrixScaleXYZF(&self->buttonMatrix, self->buttonMatScale, self->buttonMatScale, 1.0);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            if (self->isPauseMenu) {
                self->buttons[SETTINGSSCREEN_BTN_SDON]->y  = -1000.0;
                self->buttons[SETTINGSSCREEN_BTN_SDOFF]->y = -1000.0;
            }
            self->label->renderMatrix = self->buttonMatrix;
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            SettingsScreen_PlaceGameplayButton(self);
            self->timer += Engine.deltaTime;
            if (self->timer > 0.5) {
                self->alpha    = 256;
                self->timer    = 0.0;
                self->state    = SETTINGSSCREEN_STATE_MAIN;
                self->selected = usePhysicalControls == true;
            }
            break;
        case SETTINGSSCREEN_STATE_MAIN:
            CheckKeyDown(&keyDown);
            CheckKeyPress(&keyPress);
            SetRenderMatrix(&self->tempMatrix);
            SettingsScreen_PlaceGameplayButton(self);
            if (usePhysicalControls) {
                if (touches <= 0) {
                    if (keyPress.up) {
                        PlaySfxByName("Menu Move", false);
                        self->selected--;
                        if ((Engine.gameType != GAME_SONIC1 && Engine.gameType != GAME_SONICCD || self->isPauseMenu) && self->selected == SETTINGSSCREEN_SEL_SPINDASH)
                            self->selected = SETTINGSSCREEN_SEL_SFXVOL;
                        if (self->selected <= SETTINGSSCREEN_SEL_NONE)
                            self->selected = Engine.gameType == GAME_SONICCD ? SETTINGSSCREEN_SEL_GAMEPLAY : SETTINGSSCREEN_SEL_CONTROLS;
                    }
                    if (keyPress.down) {
                        PlaySfxByName("Menu Move", false);
                        self->selected++;
                        if ((Engine.gameType != GAME_SONIC1 && Engine.gameType != GAME_SONICCD || self->isPauseMenu) && self->selected == SETTINGSSCREEN_SEL_SPINDASH)
                            self->selected = SETTINGSSCREEN_SEL_REGION;
                        if (self->selected > (Engine.gameType == GAME_SONICCD ? SETTINGSSCREEN_SEL_GAMEPLAY : SETTINGSSCREEN_SEL_CONTROLS))
                            self->selected = SETTINGSSCREEN_SEL_MUSVOL;
                    }
                    for (int i = SETTINGSSCREEN_BTN_SDON; i < SETTINGSSCREEN_BTN_COUNT; ++i) self->buttons[i]->state = PUSHBUTTON_STATE_UNSELECTED;

                    switch (self->selected) {
                        case SETTINGSSCREEN_SEL_MUSVOL:
                            self->buttons[SETTINGSSCREEN_BTN_MUSUP]->state   = keyDown.left == true;
                            self->buttons[SETTINGSSCREEN_BTN_MUSDOWN]->state = keyDown.right == true;
                            if (keyPress.left) {
                                PlaySfxByName("Menu Move", false);
                                if (saveGame->musVolume > 0)
                                    saveGame->musVolume -= (MAX_VOLUME / 5);
                                SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                                if (!saveGame->musVolume)
                                    musicEnabled = false;
                            }
                            else if (keyPress.right) {
                                PlaySfxByName("Menu Move", false);
                                if (saveGame->musVolume < MAX_VOLUME)
                                    saveGame->musVolume += (MAX_VOLUME / 5);
                                if (!musicEnabled) {
                                    musicEnabled = true;
                                    if (!self->isPauseMenu)
                                        PlayMusic(0, 0);
                                }
                                SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                            }
                            break;
                        case SETTINGSSCREEN_SEL_SFXVOL:
                            self->buttons[SETTINGSSCREEN_BTN_SFXUP]->state   = keyDown.left == true;
                            self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->state = keyDown.right == true;
                            if (keyPress.left) {
                                PlaySfxByName("Menu Move", false);
                                if (saveGame->sfxVolume > 0)
                                    saveGame->sfxVolume -= (MAX_VOLUME / 5);
                                SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                            }
                            else if (keyPress.right) {
                                PlaySfxByName("Menu Move", false);
                                if (saveGame->sfxVolume < MAX_VOLUME)
                                    saveGame->sfxVolume += (MAX_VOLUME / 5);
                                SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                            }
                            break;
                        case SETTINGSSCREEN_SEL_SPINDASH: {
#if !RETRO_USE_ORIGINAL_CODE
                            bool isCD   = Engine.gameType == GAME_SONICCD;
                            int &toggle = isCD ? saveGame->soundtrackJP : saveGame->spindashEnabled;
#else
                            int &toggle = saveGame->spindashEnabled;
#endif
                            if (toggle)
                                self->buttons[SETTINGSSCREEN_BTN_SDON]->state = PUSHBUTTON_STATE_SELECTED;
                            else
                                self->buttons[SETTINGSSCREEN_BTN_SDOFF]->state = PUSHBUTTON_STATE_SELECTED;
                            if (keyPress.left || keyPress.right) {
                                PlaySfxByName("Menu Move", false);
                                if (toggle) {
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->state            = PUSHBUTTON_STATE_UNSELECTED;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->state           = PUSHBUTTON_STATE_SELECTED;
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColor          = 0x006020;
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColorSelected  = 0x00C060;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColor         = 0x00A048;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColorSelected = 0x00C060;
                                    toggle                                                   = false;
                                }
                                else {
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->state            = PUSHBUTTON_STATE_SELECTED;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->state           = PUSHBUTTON_STATE_UNSELECTED;
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColor          = 0x00A048;
                                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColorSelected  = 0x00C060;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColor         = 0x006020;
                                    self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColorSelected = 0x00C060;
                                    toggle                                                   = true;
                                }
#if !RETRO_USE_ORIGINAL_CODE
                                if (isCD) {
                                    SetGlobalVariableByName("options.soundtrack", toggle ? 0 : 1);
                                    WriteSaveRAMData();
                                }
#endif
                            }
                            break;
                        }
                        case SETTINGSSCREEN_SEL_REGION:
                            if (keyPress.left || keyPress.right) {
                                if (keyPress.left) {
                                    PlaySfxByName("Menu Move", false);
                                    if (saveGame->boxRegion - 1 >= 0)
                                        saveGame->boxRegion--;
                                }
                                else {
                                    PlaySfxByName("Menu Move", false);
                                    if (saveGame->boxRegion + 1 < 3)
                                        saveGame->boxRegion++;
                                }

                                self->buttons[SETTINGSSCREEN_BTN_JP]->bgColor                               = 0x006020;
                                self->buttons[SETTINGSSCREEN_BTN_JP]->bgColorSelected                       = 0x00C060;
                                self->buttons[SETTINGSSCREEN_BTN_US]->bgColor                               = 0x006020;
                                self->buttons[SETTINGSSCREEN_BTN_US]->bgColorSelected                       = 0x00C060;
                                self->buttons[SETTINGSSCREEN_BTN_EU]->bgColor                               = 0x006020;
                                self->buttons[SETTINGSSCREEN_BTN_EU]->bgColorSelected                       = 0x00C060;
                                self->buttons[SETTINGSSCREEN_BTN_JP + saveGame->boxRegion]->bgColor         = 0x00A048;
                                self->buttons[SETTINGSSCREEN_BTN_JP + saveGame->boxRegion]->bgColorSelected = 0x00C060;
                            }
                            self->buttons[SETTINGSSCREEN_BTN_JP + saveGame->boxRegion]->state = PUSHBUTTON_STATE_SELECTED;
                            break;
                        case SETTINGSSCREEN_SEL_CONTROLS:
                            self->buttons[SETTINGSSCREEN_BTN_CTRLS]->state = PUSHBUTTON_STATE_SELECTED;
                            if (keyPress.right && Engine.gameType == GAME_SONICCD) {
                                PlaySfxByName("Menu Move", false);
                                self->selected = SETTINGSSCREEN_SEL_GAMEPLAY;
                            }
                            else if (keyPress.start || keyPress.A) {
                                PlaySfxByName("Menu Select", false);
                                self->buttons[SETTINGSSCREEN_BTN_CTRLS]->state = PUSHBUTTON_STATE_FLASHING;
                                self->state                                    = SETTINGSSCREEN_STATE_ENTERCTRLS;
                            }
                            break;
                        case SETTINGSSCREEN_SEL_GAMEPLAY:
                            self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->state = PUSHBUTTON_STATE_SELECTED;
                            if (keyPress.left) {
                                PlaySfxByName("Menu Move", false);
                                self->selected = SETTINGSSCREEN_SEL_CONTROLS;
                            }
                            else if (keyPress.start || keyPress.A) {
                                PlaySfxByName("Menu Select", false);
                                self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->state = PUSHBUTTON_STATE_FLASHING;
                                self->state                                       = SETTINGSSCREEN_STATE_ENTERGAMEPLAY;
                            }
                            break;
                        default: break;
                    }
                    if (self->state == SETTINGSSCREEN_STATE_MAIN && keyPress.B) {
                        PlaySfxByName("Menu Back", false);
                        self->backPressed = false;
                        self->state       = SETTINGSSCREEN_STATE_EXIT;
                    }
                }
                else {
                    usePhysicalControls = false;
                    self->selected      = SETTINGSSCREEN_SEL_NONE;
                }
            }
            else {
                if (touches <= 0) {
                    if (self->buttons[SETTINGSSCREEN_BTN_MUSUP]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_MUSUP]->state = PUSHBUTTON_STATE_UNSELECTED;
                        if (saveGame->musVolume > 0) {
                            saveGame->musVolume -= (MAX_VOLUME / 5);
                        }
                        SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                        if (!saveGame->musVolume)
                            musicEnabled = false;
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_MUSDOWN]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_MUSDOWN]->state = PUSHBUTTON_STATE_UNSELECTED;
                        if (saveGame->musVolume < MAX_VOLUME) {
                            saveGame->musVolume += (MAX_VOLUME / 5);
                        }
                        if (!musicEnabled) {
                            musicEnabled = true;
                            PlayMusic(0, 0);
                        }
                        SetGameVolumes(saveGame->musVolume, saveGame->sfxVolume);
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_SFXUP]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_SFXUP]->state = PUSHBUTTON_STATE_UNSELECTED;
                        sfxVolume                                      = saveGame->sfxVolume;
                        if (sfxVolume > 0) {
                            sfxVolume -= (MAX_VOLUME / 5);
                            saveGame->sfxVolume = sfxVolume;
                        }
                        SetGameVolumes(saveGame->musVolume, sfxVolume);
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->state = PUSHBUTTON_STATE_UNSELECTED;
                        sfxVolume                                        = saveGame->sfxVolume;
                        if (sfxVolume < MAX_VOLUME) {
                            sfxVolume += (MAX_VOLUME / 5);
                            saveGame->sfxVolume = sfxVolume;
                        }
                        SetGameVolumes(saveGame->musVolume, sfxVolume);
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_SDON]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
#if !RETRO_USE_ORIGINAL_CODE
                        if (Engine.gameType == GAME_SONICCD) {
                            saveGame->soundtrackJP = true;
                            SetGlobalVariableByName("options.soundtrack", 0);
                            WriteSaveRAMData();
                        }
                        else
#endif
                            saveGame->spindashEnabled = true;
                        self->buttons[SETTINGSSCREEN_BTN_SDON]->state            = PUSHBUTTON_STATE_UNSELECTED;
                        self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColor          = 0x00A048;
                        self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColorSelected  = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColorSelected = 0x00C060;
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_SDOFF]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
#if !RETRO_USE_ORIGINAL_CODE
                        if (Engine.gameType == GAME_SONICCD) {
                            saveGame->soundtrackJP = false;
                            SetGlobalVariableByName("options.soundtrack", 1);
                            WriteSaveRAMData();
                        }
                        else
#endif
                            saveGame->spindashEnabled = false;
                        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->state           = PUSHBUTTON_STATE_UNSELECTED;
                        self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColor          = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColorSelected  = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColor         = 0x00A048;
                        self->buttons[SETTINGSSCREEN_BTN_SDOFF]->bgColorSelected = 0x00C060;
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_JP]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_JP]->state           = PUSHBUTTON_STATE_UNSELECTED;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColor         = 0x00A048;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColorSelected = 0x00C060;
                        saveGame->boxRegion                                   = REGION_JP;
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_US]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_US]->state           = PUSHBUTTON_STATE_UNSELECTED;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColor         = 0x00A048;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColorSelected = 0x00C060;
                        saveGame->boxRegion                                   = REGION_US;
                    }

                    if (self->buttons[SETTINGSSCREEN_BTN_EU]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Move", false);
                        self->buttons[SETTINGSSCREEN_BTN_EU]->state           = PUSHBUTTON_STATE_UNSELECTED;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_JP]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColor         = 0x006020;
                        self->buttons[SETTINGSSCREEN_BTN_US]->bgColorSelected = 0x00C060;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColor         = 0x00A048;
                        self->buttons[SETTINGSSCREEN_BTN_EU]->bgColorSelected = 0x00C060;
                        saveGame->boxRegion                                   = REGION_EU;
                    }
                    if (self->buttons[SETTINGSSCREEN_BTN_CTRLS]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Select", false);
                        self->buttons[SETTINGSSCREEN_BTN_CTRLS]->state = PUSHBUTTON_STATE_FLASHING;
                        self->state                                    = SETTINGSSCREEN_STATE_ENTERCTRLS;
                    }
                    if (self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->state == PUSHBUTTON_STATE_SELECTED) {
                        PlaySfxByName("Menu Select", false);
                        self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->state = PUSHBUTTON_STATE_FLASHING;
                        self->state                                       = SETTINGSSCREEN_STATE_ENTERGAMEPLAY;
                    }
                    if (self->backPressed || keyPress.B) {
                        PlaySfxByName("Menu Back", false);
                        self->backPressed = false;
                        self->state       = SETTINGSSCREEN_STATE_EXIT;
                    }
                    else if (self->state == SETTINGSSCREEN_STATE_MAIN) {
                        if (keyDown.up) {
                            self->selected      = SETTINGSSCREEN_SEL_CONTROLS;
                            usePhysicalControls = true;
                        }
                        else if (keyDown.down) {
                            self->selected      = SETTINGSSCREEN_SEL_MUSVOL;
                            usePhysicalControls = true;
                        }
                    }
                }
                else {
                    float touchX[] = { 32, 108, 32, 108, 32.0, 96, 4, 52, 100, self->buttons[SETTINGSSCREEN_BTN_CTRLS]->x, self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->x };
                    float touchY[] = { 54, 54, 22, 22, -10, -10, -42, -42, -42, self->buttons[SETTINGSSCREEN_BTN_CTRLS]->y, self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->y };

                    for (int i = 0; i <= SETTINGSSCREEN_BTN_GAMEPLAY; ++i) {
                        NativeEntity_PushButton *button = self->buttons[i];

                        if (i == SETTINGSSCREEN_BTN_GAMEPLAY && Engine.gameType != GAME_SONICCD) {
                            button->state = PUSHBUTTON_STATE_UNSELECTED;
                            continue;
                        }

                        if (i == 4 || i == 5) {
                            if (!self->isPauseMenu && (Engine.gameType == GAME_SONIC1 || Engine.gameType == GAME_SONICCD))
                                button->state = CheckTouchRect(touchX[i], touchY[i], (button->textWidth + (button->scale * 64.0)) * 0.75, 12.0) >= 0;
                        }
                        else {
                            button->state = CheckTouchRect(touchX[i], touchY[i], (button->textWidth + (button->scale * 64.0)) * 0.75, 12.0) >= 0;
                        }
                    }

                    self->backPressed = CheckTouchRect(128.0, -92.0, 32.0, 32.0) >= 0;
                }
            }
            break;
        case SETTINGSSCREEN_STATE_ENTERCTRLS:
            SetRenderMatrix(&self->buttonMatrix);
            if (self->buttons[SETTINGSSCREEN_BTN_CTRLS]->state == PUSHBUTTON_STATE_UNSELECTED) {
                byte physicalControls = GetGlobalVariableByName("options.physicalControls");
                if (Engine.gameDeviceType == RETRO_STANDARD)
                    self->state = SETTINGSSCREEN_STATE_FLIP_CTRLS;
                else
                    self->state = (physicalControls == CTRLS_MOGA || physicalControls == CTRLS_MOGAPRO) ? SETTINGSSCREEN_STATE_FLIP_CTRLS
                                                                                                        : SETTINGSSCREEN_STATE_FLIP_CTRLSTOUCH;
            }
            break;
        case SETTINGSSCREEN_STATE_FLIP_CTRLSTOUCH:
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -(M_PI / 2)) {
                self->state      = SETTINGSSCREEN_STATE_FINISHFLIP_CTRLSTOUCH;
                self->buttonRotY = -(M_PI / 2 + M_PI);
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_CONFIGDPAD) {
                    RestoreNativeObjectsSettings();
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_MAIN;
                    SetStringToFont(self->label->text, strSettings, FONT_HEADING);
                    if (self->isPauseMenu)
                        SetGlobalVariableByName("options.touchControls", true);
                }
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN) {
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_CONFIGDPAD;
                    SetStringToFont(self->label->text, strCustomizeDPad, FONT_HEADING);
                    BackupNativeObjectsSettings();
                    if (Engine.language == RETRO_JP || Engine.language == RETRO_RU)
                        self->label->scale = 0.15;
                    else
                        self->label->scale = 0.2;
                    for (int i = SETTINGSSCREEN_BTN_SDON; i < SETTINGSSCREEN_BTN_COUNT; ++i) RemoveNativeObject(self->buttons[i]);
                    self->buttons[SETTINGSSCREEN_BTN_SFXUP]->y              = self->buttons[SETTINGSSCREEN_BTN_SFXUP]->y + 4.0;
                    self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->y            = self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->y + 4.0;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]                  = CREATE_ENTITY(PushButton);
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->useRenderMatrix = true;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->x               = 88.0;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->y               = 6.0;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->z               = 0.0;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->scale           = 0.175;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColor         = 0x00A048;
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->bgColorSelected = 0x00C060;
                    SetStringToFont8(self->buttons[SETTINGSSCREEN_BTN_SDON]->text, "RESET", FONT_LABEL);
                    SetStringToFont(self->musicText, strDPadSize, FONT_LABEL);
                    SetStringToFont(self->sfxText, strDPadOpacity, FONT_LABEL);
                    self->virtualDPad           = CREATE_ENTITY(VirtualDPad);
                    self->virtualDPad           = self->virtualDPad;
                    self->virtualDPad->editMode = true;
                    SetGlobalVariableByName("options.touchControls", true);
                }
            }
            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        case SETTINGSSCREEN_STATE_FINISHFLIP_CTRLSTOUCH:
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -M_PI_2) {
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_CONFIGDPAD)
                    self->state = SETTINGSSCREEN_STATE_CTRLS_TOUCH;
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN)
                    self->state = SETTINGSSCREEN_STATE_MAIN;
                self->buttonRotY = 0.0;
            }
            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        case SETTINGSSCREEN_STATE_CTRLS_TOUCH:
            CheckKeyDown(&keyDown);
            CheckKeyPress(&keyPress);
            SetRenderMatrix(&self->tempMatrix);

            if (touches > 0) {

                // Size -
                NativeEntity_PushButton *buttonDec = self->buttons[SETTINGSSCREEN_BTN_MUSUP];
                if (CheckTouchRect(32.0, 54.0, ((64.0 * buttonDec->scale) + buttonDec->textWidth) * 0.75, 12.0) >= 0) {
                    buttonDec->state = PUSHBUTTON_STATE_SELECTED;
                    if (saveGame->vDPadSize > 0x20)
                        saveGame->vDPadSize -= 4;
                    self->virtualDPad              = self->virtualDPad;
                    self->virtualDPad->moveSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->jumpSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->pressedSize = saveGame->vDPadSize * (1.0f / 256) * 0.85;
                }
                else {
                    buttonDec->state = PUSHBUTTON_STATE_UNSELECTED;
                }

                // Size +
                NativeEntity_PushButton *buttonInc = self->buttons[SETTINGSSCREEN_BTN_MUSDOWN];
                if (CheckTouchRect(108.0, 54.0, ((64.0 * buttonInc->scale) + buttonInc->textWidth) * 0.75, 12.0) >= 0) {
                    buttonInc->state = PUSHBUTTON_STATE_SELECTED;
                    if (saveGame->vDPadSize < 0x80)
                        saveGame->vDPadSize += 4;
                    self->virtualDPad              = self->virtualDPad;
                    self->virtualDPad->moveSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->jumpSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->pressedSize = saveGame->vDPadSize * (1.0f / 256) * 0.85;
                }
                else {
                    self->buttons[SETTINGSSCREEN_BTN_MUSDOWN]->state = PUSHBUTTON_STATE_UNSELECTED;
                }

                // Opacity -
                buttonDec = self->buttons[SETTINGSSCREEN_BTN_SFXUP];
                if (CheckTouchRect(32.0, 26.0, ((64.0 * buttonDec->scale) + buttonDec->textWidth) * 0.75, 12.0) >= 0) {
                    buttonDec->state = PUSHBUTTON_STATE_SELECTED;
                    if (saveGame->vDPadOpacity > 0) {
                        saveGame->vDPadOpacity -= 4;
                    }
                }
                else {
                    buttonDec->state = PUSHBUTTON_STATE_UNSELECTED;
                }

                // Opacity +
                buttonInc = self->buttons[SETTINGSSCREEN_BTN_SFXDOWN];
                if (CheckTouchRect(108.0, 26.0, ((64.0 * buttonInc->scale) + buttonInc->textWidth) * 0.75, 12.0) >= 0) {
                    buttonInc->state = PUSHBUTTON_STATE_SELECTED;
                    if (saveGame->vDPadOpacity < 0x100) {
                        saveGame->vDPadOpacity += 4;
                    }
                }
                else {
                    buttonInc->state = PUSHBUTTON_STATE_UNSELECTED;
                }

                self->virtualDPad->alpha = saveGame->vDPadOpacity;

                NativeEntity_PushButton *button = self->buttons[SETTINGSSCREEN_BTN_SDON];
                button->state                   = CheckTouchRect(88.0, -2.0, ((64.0 * button->scale) + button->textWidth) * 0.75, 12.0) >= 0;

                int moveTouch, jumpTouch;

                if (self->virtualDPad->moveFinger == -1)
                    moveTouch = CheckTouchRect(self->virtualDPad->moveX, self->virtualDPad->moveY, 128.0 * self->virtualDPad->moveSize,
                                               128.0 * self->virtualDPad->moveSize);
                else
                    moveTouch = CheckTouchRect(-0.5 * SCREEN_CENTERX_F, SCREEN_CENTERY_F * -0.5, 0.5 * SCREEN_CENTERX_F, SCREEN_CENTERY_F * 0.5);

                if (moveTouch >= 0) {
                    if (self->virtualDPad->moveFinger == -1) {
                        self->virtualDPad->moveFinger = moveTouch;
                        self->virtualDPad->relativeX  = self->virtualDPad->moveX - touchXF[moveTouch];
                        self->virtualDPad->relativeY  = self->virtualDPad->moveY - touchYF[moveTouch];
                    }

                    float moveSizeScale      = 128.0 * self->virtualDPad->moveSize;
                    self->virtualDPad->moveX = touchXF[moveTouch] + self->virtualDPad->relativeX;
                    self->virtualDPad->moveY = touchYF[moveTouch] + self->virtualDPad->relativeY;
                    if (-SCREEN_CENTERX_F > (self->virtualDPad->moveX - moveSizeScale))
                        self->virtualDPad->moveX = moveSizeScale - SCREEN_CENTERX_F;
                    if (self->virtualDPad->moveX + moveSizeScale > 0.0f)
                        self->virtualDPad->moveX = -moveSizeScale;

                    if (self->virtualDPad->moveY + moveSizeScale > -8.0f)
                        self->virtualDPad->moveY = -8.0f - moveSizeScale;

                    if (-SCREEN_CENTERY_F > self->virtualDPad->moveY - moveSizeScale)
                        self->virtualDPad->moveY = moveSizeScale - SCREEN_CENTERY_F;
                }
                if (self->virtualDPad->moveFinger == -1) {
                    if (self->virtualDPad->jumpFinger == -1)
                        jumpTouch = CheckTouchRect(self->virtualDPad->jumpX, self->virtualDPad->jumpY, 128.0 * self->virtualDPad->jumpSize,
                                                   128.0 * self->virtualDPad->jumpSize);
                    else
                        jumpTouch = CheckTouchRect(SCREEN_CENTERX_F * 0.5, SCREEN_CENTERY_F * -0.5, SCREEN_CENTERX_F * 0.5, 0.5 * SCREEN_CENTERY_F);

                    if (jumpTouch >= 0) {
                        self->virtualDPad = self->virtualDPad;
                        if (self->virtualDPad->jumpFinger == -1) {
                            self->virtualDPad->jumpFinger = jumpTouch;
                            self->virtualDPad->relativeX  = self->virtualDPad->jumpX - touchXF[jumpTouch];
                            self->virtualDPad->relativeY  = self->virtualDPad->jumpY - touchYF[jumpTouch];
                        }

                        float jumpScaleSize      = 128.0 * self->virtualDPad->jumpSize;
                        self->virtualDPad->jumpX = touchXF[jumpTouch] + self->virtualDPad->relativeX;
                        self->virtualDPad->jumpY = touchYF[jumpTouch] + self->virtualDPad->relativeY;

                        if (self->virtualDPad->jumpX - jumpScaleSize < 0.0f)
                            self->virtualDPad->jumpX = jumpScaleSize;
                        if (self->virtualDPad->jumpX + jumpScaleSize > SCREEN_CENTERX_F)
                            self->virtualDPad->jumpX = SCREEN_CENTERX_F - jumpScaleSize;

                        if (self->virtualDPad->jumpY + jumpScaleSize > -8.0f)
                            self->virtualDPad->jumpY = -8.0 - jumpScaleSize;
                        if (-SCREEN_CENTERY_F > self->virtualDPad->jumpY - jumpScaleSize)
                            self->virtualDPad->jumpY = jumpScaleSize - SCREEN_CENTERY_F;
                    }
                }
                self->backPressed = CheckTouchRect(136.0, 88.0, 32.0, 16.0) >= 0;
            }
            else {
                self->virtualDPad                                = self->virtualDPad;
                self->virtualDPad->moveFinger                    = -1;
                self->virtualDPad->jumpFinger                    = -1;
                self->buttons[SETTINGSSCREEN_BTN_MUSUP]->state   = PUSHBUTTON_STATE_UNSELECTED;
                self->buttons[SETTINGSSCREEN_BTN_MUSDOWN]->state = PUSHBUTTON_STATE_UNSELECTED;
                self->buttons[SETTINGSSCREEN_BTN_SFXUP]->state   = PUSHBUTTON_STATE_UNSELECTED;
                self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->state = PUSHBUTTON_STATE_UNSELECTED;
                if (self->backPressed) {
                    PlaySfxByName("Menu Back", false);
                    self->backPressed = false;
                    self->state       = SETTINGSSCREEN_STATE_FLIP_CTRLSTOUCH;
                    SetGlobalVariableByName("options.touchControls", false);
                    saveGame->vDPadX_Move = (self->virtualDPad->moveX + SCREEN_CENTERX_F);
                    saveGame->vDPadY_Move = -(self->virtualDPad->moveY - SCREEN_CENTERY_F);
                    saveGame->vDPadX_Jump = self->virtualDPad->jumpX - SCREEN_CENTERX_F;
                    saveGame->vDPadY_Jump = -(self->virtualDPad->jumpY - SCREEN_CENTERY_F);
                }
                if (self->buttons[SETTINGSSCREEN_BTN_SDON]->state == PUSHBUTTON_STATE_SELECTED) {
                    self->buttons[SETTINGSSCREEN_BTN_SDON]->state = PUSHBUTTON_STATE_UNSELECTED;
                    PlaySfxByName("Event", false);
                    saveGame->vDPadSize            = 64;
                    saveGame->vDPadX_Move          = 56;
                    saveGame->vDPadY_Move          = 184;
                    saveGame->vDPadX_Jump          = -56;
                    saveGame->vDPadY_Jump          = 188;
                    saveGame->vDPadOpacity         = 160;
                    self->virtualDPad->alpha       = 160;
                    self->virtualDPad->moveX       = saveGame->vDPadX_Move - SCREEN_CENTERX_F;
                    self->virtualDPad->moveY       = -(saveGame->vDPadY_Move - SCREEN_CENTERY_F);
                    self->virtualDPad->jumpX       = saveGame->vDPadX_Jump + SCREEN_CENTERX_F;
                    self->virtualDPad->jumpY       = -(saveGame->vDPadY_Jump - SCREEN_CENTERY_F);
                    self->virtualDPad->moveFinger  = -1;
                    self->virtualDPad->jumpFinger  = -1;
                    self->virtualDPad->moveSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->jumpSize    = saveGame->vDPadSize * (1.0f / 256);
                    self->virtualDPad->pressedSize = saveGame->vDPadSize * (1.0f / 256) * 0.85;
                }
            }

            if (self->state == SETTINGSSCREEN_STATE_CTRLS_TOUCH && keyPress.B) {
                PlaySfxByName("Menu Back", false);
                self->backPressed = false;
                self->state       = SETTINGSSCREEN_STATE_FLIP_CTRLSTOUCH;
                SetGlobalVariableByName("options.touchControls", false);
                saveGame->vDPadX_Move = (self->virtualDPad->moveX + SCREEN_CENTERX_F);
                saveGame->vDPadY_Move = -(self->virtualDPad->moveY - SCREEN_CENTERY_F);
                saveGame->vDPadX_Jump = self->virtualDPad->jumpX - SCREEN_CENTERX_F;
                saveGame->vDPadY_Jump = -(self->virtualDPad->jumpY - SCREEN_CENTERY_F);
            }
            break;
        case SETTINGSSCREEN_STATE_ENTERGAMEPLAY:
            SetRenderMatrix(&self->buttonMatrix);
            if (self->buttons[SETTINGSSCREEN_BTN_GAMEPLAY]->state == PUSHBUTTON_STATE_UNSELECTED)
                self->state = SETTINGSSCREEN_STATE_FLIP_GAMEPLAY;
            break;
        case SETTINGSSCREEN_STATE_FLIP_GAMEPLAY: {
            SettingsScreen_PlaceGameplayRows(self);
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -(M_PI / 2)) {
                self->state      = SETTINGSSCREEN_STATE_FINISHFLIP_GAMEPLAY;
                self->buttonRotY = -(M_PI / 2 + M_PI);
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_GAMEPLAY) {
                    // back to the main page
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_MAIN;
                    SetStringToFont(self->label->text, strSettings, FONT_HEADING);
                    SettingsScreen_ShowGameplay(self, false);
                    WriteSaveRAMData();
                }
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN) {
                    // to the Gameplay panel
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_GAMEPLAY;
                    SetStringToFont8(self->label->text, "GAMEPLAY", FONT_HEADING);
                    SettingsScreen_UpdateGameplayButtons(self);
                    SettingsScreen_ShowGameplay(self, true);
                    self->gpSelected = 0;
                }
            }

            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        }
        case SETTINGSSCREEN_STATE_FINISHFLIP_GAMEPLAY:
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -(M_PI_2)) {
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_GAMEPLAY)
                    self->state = SETTINGSSCREEN_STATE_GAMEPLAY;
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN)
                    self->state = SETTINGSSCREEN_STATE_MAIN;
                self->buttonRotY = 0.0;
            }
            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        case SETTINGSSCREEN_STATE_GAMEPLAY: {
            SettingsScreen_PlaceGameplayRows(self);
            if (self->gpNoteK > 0.0f && self->gpNoteK < 1.0f)
                self->gpNoteK = fminf(self->gpNoteK + ((1.05f - self->gpNoteK) / ((60.0f * (float)Engine.deltaTime) * 8.0f)), 1.0f);
            CheckKeyDown(&keyDown);
            CheckKeyPress(&keyPress);
            SetRenderMatrix(&self->tempMatrix);
            if (usePhysicalControls) {
                if (touches <= 0) {
                    if (keyPress.up) {
                        PlaySfxByName("Menu Move", false);
                        self->gpSelected = self->gpSelected == 1 ? 3 : self->gpSelected - 1;
                    }
                    if (keyPress.down) {
                        PlaySfxByName("Menu Move", false);
                        self->gpSelected = self->gpSelected == 3 ? 1 : self->gpSelected + 1;
                    }
                    if (keyPress.left || keyPress.right) {
                        PlaySfxByName("Menu Move", false);
                        SettingsScreen_SetGameplay(self, self->gpSelected, keyPress.left ? 0 : 1);
                    }
                    for (int i = SETTINGSSCREEN_BTN_GP_SPINDASH_ON; i < SETTINGSSCREEN_BTN_COUNT; ++i) self->buttons[i]->state = PUSHBUTTON_STATE_UNSELECTED;
                    self->buttons[SETTINGSSCREEN_BTN_GP_STYLE_CD + self->gpSelected * 2 + SettingsScreen_GameplayActive(self->gpSelected)]->state =
                        PUSHBUTTON_STATE_SELECTED;
                    if (keyPress.B) {
                        PlaySfxByName("Menu Back", false);
                        self->backPressed = false;
                        self->gpNoteK     = 0.0f;
                        self->state       = SETTINGSSCREEN_STATE_FLIP_GAMEPLAY;
                    }
                }
                else {
                    usePhysicalControls = false;
                }
            }
            else {
                if (touches <= 0) {
                    for (int i = SETTINGSSCREEN_BTN_GP_SPINDASH_ON; i < SETTINGSSCREEN_BTN_COUNT; ++i) {
                        if (self->buttons[i]->state == PUSHBUTTON_STATE_SELECTED) {
                            PlaySfxByName("Menu Move", false);
                            self->buttons[i]->state = PUSHBUTTON_STATE_UNSELECTED;
                            int idx                 = i - SETTINGSSCREEN_BTN_GP_STYLE_CD;
                            SettingsScreen_SetGameplay(self, idx / 2, idx % 2);
                        }
                    }
                    if (self->backPressed || keyPress.B) {
                        PlaySfxByName("Menu Back", false);
                        self->backPressed = false;
                        self->gpNoteK     = 0.0f;
                        self->state       = SETTINGSSCREEN_STATE_FLIP_GAMEPLAY;
                    }
                    else if (keyDown.up || keyDown.down) {
                        usePhysicalControls = true;
                        self->gpSelected    = 1;
                    }
                }
                else {
                    for (int i = SETTINGSSCREEN_BTN_GP_SPINDASH_ON; i < SETTINGSSCREEN_BTN_COUNT; ++i) {
                        NativeEntity_PushButton *b = self->buttons[i];
                        b->state = CheckTouchRect(b->x, b->y - 8.0f, (b->textWidth + (b->scale * 64.0f)) * 0.75f, 12.0f) >= 0;
                    }
                    self->backPressed = CheckTouchRect(128.0, -92.0, 32.0, 32.0) >= 0;
                }
            }
            break;
        }
        case SETTINGSSCREEN_STATE_EXIT: {
            if (self->alpha > 0)
                self->alpha -= 8;

            if (self->timer < 0.2)
                self->buttonMatScale = fmaxf(self->buttonMatScale + ((1.5f - self->buttonMatScale) / ((Engine.deltaTime * 60.0) * 8.0)), 0.0);
            else
                self->buttonMatScale = fmaxf(self->buttonMatScale + ((-1.0f - self->buttonMatScale) / ((Engine.deltaTime * 60.0) * 8.0)), 0.0);

            NewRenderState();
            MatrixScaleXYZF(&self->buttonMatrix, self->buttonMatScale, self->buttonMatScale, 1.0);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            self->label->renderMatrix = self->buttonMatrix;
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;

            self->timer += Engine.deltaTime;
            if (self->timer > 0.5) {
                optionsMenu->state = OPTIONSMENU_STATE_EXITSUBMENU;
                for (int i = 0; i < SETTINGSSCREEN_BTN_COUNT; ++i) RemoveNativeObject(self->buttons[i]);

                RemoveNativeObject(self->label);
                RemoveNativeObject(self);
                Engine.gameMode = ENGINE_MAINGAME;
#if !RETRO_USE_ORIGINAL_CODE
                if (Engine.gameType == GAME_SONICCD) {
                    // Sonic CD: the CD / S2 style comes from the Gameplay screen
                    SetGlobalVariableByName("options.originalControls", saveGame->gameplayCD ? 1 : 0);
                    SetGlobalVariableByName("options.airSpeedCap", saveGame->gameplayCD ? 1 : 0);
                }
                else
#endif
                if (saveGame->spindashEnabled) {
                    SetGlobalVariableByName("options.originalControls", false);
                    SetGlobalVariableByName("options.airSpeedCap", false);
                }
                else {
                    SetGlobalVariableByName("options.originalControls", true);
                    SetGlobalVariableByName("options.airSpeedCap", true);
                }

                if (Engine.globalBoxRegion != saveGame->boxRegion) {
                    int boxTex = 0, cartTex = 0;
                    if (Engine.gameType == GAME_SONICCD) {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: boxTex = LoadTexture("Data/Game/Models/JPBox.png", TEXFMT_RGBA8888); break;
                            case REGION_US: boxTex = LoadTexture("Data/Game/Models/USBox.png", TEXFMT_RGBA8888); break;
                            case REGION_EU: boxTex = LoadTexture("Data/Game/Models/EUBox.png", TEXFMT_RGBA8888); break;
                        }
                    }
                    else {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: boxTex = LoadTexture("Data/Game/Models/Package_JP.png", TEXFMT_RGBA8888); break;
                            case REGION_US: boxTex = LoadTexture("Data/Game/Models/Package_US.png", TEXFMT_RGBA8888); break;
                            case REGION_EU: boxTex = LoadTexture("Data/Game/Models/Package_EU.png", TEXFMT_RGBA8888); break;
                        }
                    }
                    if (Engine.gameType == GAME_SONICCD) {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: cartTex = LoadTexture("Data/Game/Models/DiscJP.png", TEXFMT_RGBA8888); break;
                            case REGION_US: cartTex = LoadTexture("Data/Game/Models/DiscUS.png", TEXFMT_RGBA8888); break;
                            case REGION_EU: cartTex = LoadTexture("Data/Game/Models/DiscEU.png", TEXFMT_RGBA8888); break;
                        }
                    }
                    else {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: cartTex = LoadTexture("Data/Game/Models/Package_JP.png", TEXFMT_RGBA8888); break;
                            case REGION_US: cartTex = LoadTexture("Data/Game/Models/Package_US.png", TEXFMT_RGBA8888); break;
                            case REGION_EU: cartTex = LoadTexture("Data/Game/Models/Package_EU.png", TEXFMT_RGBA8888); break;
                        }
                    }

                    Engine.globalBoxRegion = saveGame->boxRegion;

                    if (Engine.gameType == GAME_SONICCD) {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: ReplaceTexture("Data/Game/Models/JPBox.png", boxTex); break;
                            case REGION_US: ReplaceTexture("Data/Game/Models/USBox.png", boxTex); break;
                            case REGION_EU: ReplaceTexture("Data/Game/Models/EUBox.png", boxTex); break;
                        }
                    }
                    else {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: ReplaceTexture("Data/Game/Models/Package_JP.png", boxTex); break;
                            case REGION_US: ReplaceTexture("Data/Game/Models/Package_US.png", boxTex); break;
                            case REGION_EU: ReplaceTexture("Data/Game/Models/Package_EU.png", boxTex); break;
                        }
                    }
                    if (Engine.gameType == GAME_SONICCD) {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: ReplaceTexture("Data/Game/Models/DiscJP.png", cartTex); break;
                            case REGION_US: ReplaceTexture("Data/Game/Models/DiscUS.png", cartTex); break;
                            case REGION_EU: ReplaceTexture("Data/Game/Models/DiscEU.png", cartTex); break;
                        }
                    }
                    else {
                        switch (Engine.globalBoxRegion) {
                            case REGION_JP: ReplaceTexture("Data/Game/Models/Package_JP.png", cartTex); break;
                            case REGION_US: ReplaceTexture("Data/Game/Models/Package_US.png", cartTex); break;
                            case REGION_EU: ReplaceTexture("Data/Game/Models/Package_EU.png", cartTex); break;
                        }
                    }
                }
                WriteSaveRAMData();
                return;
            }
            break;
        }
        case SETTINGSSCREEN_STATE_FLIP_CTRLS: {
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -(M_PI / 2)) {
                self->state      = SETTINGSSCREEN_STATE_FINISHFLIP_CTRLS;
                self->buttonRotY = -(M_PI / 2 + M_PI);
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_CONTROLLER) {
                    RestoreNativeObjectsSettings();
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_MAIN;
                    SetStringToFont(self->label->text, strSettings, FONT_HEADING);
                    if (self->isPauseMenu)
                        SetGlobalVariableByName("options.touchControls", true);
                }
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN) {
                    self->stateDraw = SETTINGSSCREEN_STATEDRAW_CONTROLLER;
                    BackupNativeObjectsSettings();
                    SetStringToFont(self->label->text, strControls, FONT_HEADING);
                    for (int i = 0; i < SETTINGSSCREEN_BTN_COUNT; ++i) RemoveNativeObject(self->buttons[i]);

                    //???
                    // self->buttons[SETTINGSSCREEN_BTN_SFXUP]->y += 4.0;
                    // self->buttons[SETTINGSSCREEN_BTN_SFXDOWN]->y += 4.0;
                }
            }

            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        }
        case SETTINGSSCREEN_STATE_FINISHFLIP_CTRLS:
            self->buttonRotY -= (10.0 * Engine.deltaTime);
            if (self->buttonRotY < -(M_PI_2)) {
                if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_CONTROLLER) {
                    self->state = SETTINGSSCREEN_STATE_CTRLS;
                }
                else if (self->stateDraw == SETTINGSSCREEN_STATEDRAW_MAIN) {
                    self->state = SETTINGSSCREEN_STATE_MAIN;
                }
                self->buttonRotY = 0.0;
            }
            NewRenderState();
            MatrixRotateYF(&self->buttonMatrix, self->buttonRotY);
            MatrixTranslateXYZF(&self->tempMatrix, 0.0, -8.0, 160.0);
            MatrixMultiplyF(&self->buttonMatrix, &self->tempMatrix);
            SetRenderMatrix(&self->buttonMatrix);
            for (int l = 0; l < SETTINGSSCREEN_BTN_COUNT; ++l) self->buttons[l]->renderMatrix = self->buttonMatrix;
            break;
        case SETTINGSSCREEN_STATE_CTRLS:
            CheckKeyDown(&keyDown);
            CheckKeyPress(&keyPress);
            SetRenderMatrix(&self->tempMatrix);
            if (touches <= 0) {
                if (self->backPressed) {
                    PlaySfxByName("Menu Back", false);
                    self->backPressed = false;
                    self->state       = SETTINGSSCREEN_STATE_FLIP_CTRLS;
                    break;
                }
            }
            else {
                self->backPressed = CheckTouchRect(136.0, 88.0, 32.0, 16.0) >= 0;
            }
            if (keyPress.B) {
                PlaySfxByName("Menu Back", false);
                self->backPressed = false;
                self->state       = SETTINGSSCREEN_STATE_FLIP_CTRLS;
            }
            break;
        default: break;
    }

    RenderMesh(self->panelMesh, MESH_COLORS, false);
    switch (self->stateDraw) {
        case SETTINGSSCREEN_STATEDRAW_CONFIGDPAD:
            if (Engine.language == RETRO_JP) {
                RenderText(self->musicText, FONT_LABEL, -128.0, 58.0, 0, 0.09, 255);
                RenderText(self->sfxText, FONT_LABEL, -128.0, 30.0, 0, 0.09, 255);
            }
            else {
                RenderText(self->musicText, FONT_LABEL, -128.0, 58.0, 0, 0.125, 255);
                RenderText(self->sfxText, FONT_LABEL, -128.0, 30.0, 0, 0.125, 255);
            }
            self->virtualDPad = self->virtualDPad;
            if (self->virtualDPad->moveFinger >= 0) {
                RenderRect(-SCREEN_CENTERX_F, 0.0, 0.0, SCREEN_CENTERX_F, SCREEN_CENTERY_F, 255, 0, 0, 64);
                self->virtualDPad = self->virtualDPad;
            }
            if (self->virtualDPad->jumpFinger >= 0)
                RenderRect(0.0, 0.0, 0.0, SCREEN_CENTERX_F, SCREEN_CENTERY_F, 255, 0, 0, 64);
            break;
        case SETTINGSSCREEN_STATEDRAW_CONTROLLER:
            RenderImage(0.0, 0.0, 0.0, 0.275, 0.275, 512.0, 256.0, 1024.0, 512.0, 0.0, 0.0, 255, self->controllerTex);
            break;
        case SETTINGSSCREEN_STATEDRAW_GAMEPLAY:
            for (int r = 1; r < 4; ++r) {
                if (usePhysicalControls && self->gpSelected == r)
                    SetRenderVertexColor(0xFF, 0xFF, 0x00);
                else
                    SetRenderVertexColor(0xFF, 0xFF, 0xFF);
                RenderText(self->gameplayRowText[r], FONT_LABEL, -128.0, 58.0 - (r - 1) * 32.0, 0, gameplayLabelScale, 255);
            }
            SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            if (self->gpNoteK > 0.0f) {
                const char *noteSrc = "The changes will not take effect\runtil you restart the game";
                ushort note[128];
                SetStringToFont8(note, noteSrc, FONT_TEXT);
                float full = 290.0f / (GetTextWidth(note, FONT_TEXT, 1.0f) + 1.0f);
                if (full > 0.6f)
                    full = 0.6f;
                float ns = full * self->gpNoteK;
                SetRenderVertexColor(0xFF, 0x80, 0x00);
                RenderText(note, FONT_TEXT, GetTextWidth(note, FONT_TEXT, ns) * -0.5f, -66.0f - GetTextHeight(note, FONT_TEXT, ns) * 0.5f, 0, ns, 255);
                SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            }
            break;
        case SETTINGSSCREEN_STATEDRAW_MAIN:
            if (self->selected == SETTINGSSCREEN_SEL_MUSVOL)
                SetRenderVertexColor(0xFF, 0xFF, 0x00);
            else
                SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            RenderText(self->musicText, FONT_LABEL, -128.0, 58.0, 0, 0.125, 255);

            SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            for (int i = 0; i < 5; i++) {
                int v = (saveGame->musVolume <= i * 20) ? 128 : 255;
                RenderRect(55.0 + i * 6, 56.0 + i * 4, 0.0, 4.0, 4.0 + i * 4, v, v, v, 255);
                v = (saveGame->sfxVolume <= i * 20) ? 128 : 255;
                RenderRect(55.0 + i * 6, 24.0 + i * 4, 0.0, 4.0, 4.0 + i * 4, v, v, v, 255);
            }

            if (self->selected == SETTINGSSCREEN_SEL_SFXVOL)
                SetRenderVertexColor(0xFF, 0xFF, 0x00);
            else
                SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            if (Engine.language == RETRO_RU)
                RenderText(self->sfxText, FONT_LABEL, -128.0, 26.0, 0, 0.09, 255);
            else
                RenderText(self->sfxText, FONT_LABEL, -128.0, 26.0, 0, 0.125, 255);

            if (self->selected == SETTINGSSCREEN_SEL_SPINDASH)
                SetRenderVertexColor(0xFF, 0xFF, 0x00);
            else
                SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            if (!self->isPauseMenu && (Engine.gameType == GAME_SONIC1 || Engine.gameType == GAME_SONICCD)) {
                if ((Engine.language - 1) <= 6 && ((1 << (Engine.language - 1)) & 0x43))
                    RenderText(self->spindashText, FONT_LABEL, -128.0, -6.0, 0, 0.09, 255);
                else
                    RenderText(self->spindashText, FONT_LABEL, -128.0, -6.0, 0, 0.125, 255);
            }

            if (self->selected == SETTINGSSCREEN_SEL_REGION)
                SetRenderVertexColor(0xFF, 0xFF, 0x00);
            else
                SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            RenderText(self->boxArtText, FONT_LABEL, -128.0, -38.0, 0, 0.125, 255);
            SetRenderVertexColor(0xFF, 0xFF, 0xFF);
            break;
    }
    NewRenderState();
    SetRenderMatrix(NULL);

    switch (self->stateDraw) {
        case SETTINGSSCREEN_STATEDRAW_GAMEPLAY:
        case SETTINGSSCREEN_STATEDRAW_MAIN:
            RenderImage(128.0, -92.0, 160.0, 0.3, 0.3, 64.0, 64.0, 128.0, 128.0, 128.0, self->backPressed ? 128.0 : 0, self->alpha, self->arrowsTex);
            break;
        case SETTINGSSCREEN_STATEDRAW_CONFIGDPAD:
        case SETTINGSSCREEN_STATEDRAW_CONTROLLER:
            RenderImage(136.0, 88.0, 160.0, 0.25, 0.25, 64.0, 64.0, 128.0, 128.0, 128.0, self->backPressed ? 128.0 : 0, self->alpha, self->arrowsTex);
            break;
    }
}
