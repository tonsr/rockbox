/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id: $
 *
 * Originally by Joshua Oreman, improved by Prashant Varanasi
 * Ported to Rockbox by Ben Basha (Paprica)
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "plugin.h"
#include "xlcd.h"
#include "configfile.h"

PLUGIN_HEADER

/*Still To do:*/
/* - Make original speed and further increases in speed depend more on screen size*/
/* - attempt to make the tunnels get narrower as the game goes on*/
/* - make the chopper look better, maybe a picture, and scale according to screen size*/
/* - use textures for the color screens for background and terrain, eg stars on background*/
/* - allow choice of different levels [later: different screen themes]*/
/* - better high score handling, improved screen etc. */

#if (CONFIG_KEYPAD == IRIVER_H100_PAD) || (CONFIG_KEYPAD == IRIVER_H300_PAD)

#define QUIT BUTTON_OFF
#define ACTION BUTTON_UP
#define ACTION2 BUTTON_SELECT
#define ACTIONTEXT "SELECT"

#elif (CONFIG_KEYPAD == IPOD_3G_PAD) || \
      (CONFIG_KEYPAD == IPOD_4G_PAD)

#define QUIT BUTTON_MENU
#define ACTION BUTTON_SELECT
#define ACTIONTEXT "SELECT"

#elif CONFIG_KEYPAD == IAUDIO_X5_PAD /* grayscale at the moment */

#define QUIT BUTTON_POWER
#define ACTION BUTTON_UP
#define ACTION2 BUTTON_SELECT
#define ACTIONTEXT "SELECT"

#elif CONFIG_KEYPAD == IRIVER_H10_PAD
#define QUIT BUTTON_POWER
#define ACTION BUTTON_RIGHT
#define ACTIONTEXT "RIGHT"

#elif CONFIG_KEYPAD == SANSA_E200_PAD
#define QUIT BUTTON_POWER
#define ACTION BUTTON_SELECT
#define ACTIONTEXT "SELECT"

#elif CONFIG_KEYPAD == GIGABEAT_PAD
#define QUIT BUTTON_MENU
#define ACTION BUTTON_SELECT
#define ACTIONTEXT "SELECT"

#elif CONFIG_KEYPAD == RECORDER_PAD
#define QUIT BUTTON_OFF
#define ACTION BUTTON_PLAY
#define ACTIONTEXT "PLAY"

#elif CONFIG_KEYPAD == ONDIO_PAD
#define QUIT BUTTON_OFF
#define ACTION BUTTON_UP
#define ACTION2 BUTTON_MENU
#define ACTIONTEXT "UP"

#else
#error Unsupported keypad
#endif

static struct plugin_api* rb;

#define NUMBER_OF_BLOCKS 8
#define NUMBER_OF_PARTICLES 3
#define MAX_TERRAIN_NODES 15

#define LEVEL_MODE_NORMAL 0
#define LEVEL_MODE_STEEP 1

#define CYCLETIME 60

/*Chopper's local variables to track the terrain position etc*/
static int chopCounter;
static int iRotorOffset;
static int iScreenX;
static int iScreenY;
static int iPlayerPosX;
static int iPlayerPosY;
static int iCameraPosX;
static int iPlayerSpeedX;
static int iPlayerSpeedY;
static int iLastBlockPlacedPosX;
static int iGravityTimerCountdown;
static int iPlayerAlive;
static int iLevelMode;
static int blockh,blockw;
static int highscore;
static int score;

#define CFG_FILE "chopper.cfg"
#define MAX_POINTS 50000
static struct configdata config[] =
{
   {TYPE_INT, 0, MAX_POINTS, &highscore, "highscore", NULL, NULL}
};

struct CBlock
{
    int iWorldX;
    int iWorldY;

    int iSizeX;
    int iSizeY;

    int bIsActive;
};

struct CParticle
{
    int iWorldX;
    int iWorldY;

    int iSpeedX;
    int iSpeedY;

    int bIsActive;
};

struct CTerrainNode
{
    int x;
    int y;
};

struct CTerrain
{
    struct CTerrainNode mNodes[MAX_TERRAIN_NODES];
    int iNodesCount;
    int iLastNodePlacedPosX;
};

struct CBlock mBlocks[NUMBER_OF_BLOCKS];
struct CParticle mParticles[NUMBER_OF_PARTICLES];

struct CTerrain mGround;
struct CTerrain mRoof;

/*Function declarations*/
static void chopDrawParticle(struct CParticle *mParticle);
static void chopDrawBlock(struct CBlock *mBlock);
static void chopRenderTerrain(struct CTerrain *ter);
void chopper_load(bool newgame);
void cleanup_chopper(void);

static void chopDrawPlayer(int x,int y) /* These are SCREEN coords, not world! */
{

#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(50,50,200));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_DARKGRAY);
#endif
    rb->lcd_fillrect(x+6, y+2, 12, 9);
    rb->lcd_fillrect(x-3, y+6, 20, 3);

#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(50,50,50));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_DARKGRAY);
#endif
    rb->lcd_fillrect(x+10, y, 2, 3);
    rb->lcd_fillrect(x+10, y, 1, 3);

#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(40,40,100));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_BLACK);
#endif
    rb->lcd_drawline(x, y+iRotorOffset, x+20, y-iRotorOffset);

#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(20,20,50));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_BLACK);
#endif
    rb->lcd_fillrect(x - 2, y + 5, 2, 5);

}

static void chopClearTerrain(struct CTerrain *ter)
{
    ter->iNodesCount = -1;
}


int iR(int low,int high)
{
    return low+rb->rand()%(high-low+1);
}

static void chopCopyTerrain(struct CTerrain *src, struct CTerrain *dest,
                            int xOffset,int yOffset)
{
    int i=0;

    while(i < src->iNodesCount)
    {
        dest->mNodes[i].x = src->mNodes[i].x + xOffset;
        dest->mNodes[i].y = src->mNodes[i].y + yOffset;

        i++;
    }

    dest->iNodesCount = src->iNodesCount;
    dest->iLastNodePlacedPosX = src->iLastNodePlacedPosX;

}

static void chopAddTerrainNode(struct CTerrain *ter, int x, int y)
{
    int i=0;

    if(ter->iNodesCount + 1 >= MAX_TERRAIN_NODES)
    {
        /* DEBUGF("ERROR: Not enough nodes!\n"); */
        return;
    }

    ter->iNodesCount++;

    i = ter->iNodesCount - 1;

    ter->mNodes[i].x = x;
    ter->mNodes[i].y= y;

    ter->iLastNodePlacedPosX = x;

}

static void chopTerrainNodeDeleteAndShift(struct CTerrain *ter,int nodeIndex)
{
    int i=nodeIndex;

    while( i < ter->iNodesCount )
    {
        ter->mNodes[i - 1] = ter->mNodes[i];
        i++;
    }

    ter->iNodesCount--;


}

int chopUpdateTerrainRecycling(struct CTerrain *ter)
{
    int i=1;
    int ret = 0;
    int iNewNodePos,g,v;
    while(i < ter->iNodesCount)
    {

        if( iCameraPosX > ter->mNodes[i].x)
        {

            chopTerrainNodeDeleteAndShift(ter,i);

            iNewNodePos = ter->iLastNodePlacedPosX + 50;
            g = iScreenY - 10;

            v = 3*iPlayerSpeedX;
            if(v>50)
                v=50;
            if(iLevelMode == LEVEL_MODE_STEEP)
                v*=5;

            chopAddTerrainNode(ter,iNewNodePos,g - iR(-v,v));
            ret=1;

        }

        i++;

    }

    return 1;
}

int chopTerrainHeightAtPoint(struct CTerrain *ter, int pX)
{

    int iNodeIndexOne=0,iNodeIndexTwo=0, h, terY1, terY2, terX1, terX2, a, b;
    float c,d;

    int i=0;
    for(i=1;i<MAX_TERRAIN_NODES;i++)
    {
        if(ter->mNodes[i].x > pX)
        {
            iNodeIndexOne = i - 1;
            break;
        }

    }

    iNodeIndexTwo = iNodeIndexOne + 1;
    terY1 = ter->mNodes[iNodeIndexOne].y;
    terY2 = ter->mNodes[iNodeIndexTwo].y;

    terX1 = 0;
    terX2 = ter->mNodes[iNodeIndexTwo].x - ter->mNodes[iNodeIndexOne].x;

    pX-= ter->mNodes[iNodeIndexOne].x;

    a = terY2 - terY1;
    b = terX2;
    c = pX;
    d = (c/b) * a;

    h = d + terY1;

    return h;

}

int chopPointInTerrain(struct CTerrain *ter, int pX, int pY, int iTestType)
{
    int h = chopTerrainHeightAtPoint(ter, pX);

    if(iTestType == 0)
        return (pY > h);
    else
        return (pY < h);
}

static void chopAddBlock(int x,int y,int sx,int sy, int indexOverride)
{
    int i=0;

    if(indexOverride < 0)
    {
        while(mBlocks[i].bIsActive && i < NUMBER_OF_BLOCKS)
            i++;
        if(i==NUMBER_OF_BLOCKS)
        {
            DEBUGF("No blocks!\n");
            return;
        }
    }
    else
        i = indexOverride;

    mBlocks[i].bIsActive = 1;
    mBlocks[i].iWorldX = x;
    mBlocks[i].iWorldY = y;
    mBlocks[i].iSizeX = sx;
    mBlocks[i].iSizeY = sy;

    iLastBlockPlacedPosX = x;
}

static void chopAddParticle(int x,int y,int sx,int sy)
{
    int i=0;

    while(mParticles[i].bIsActive && i < NUMBER_OF_PARTICLES)
        i++;

    if(i==NUMBER_OF_BLOCKS)
        return;

    mParticles[i].bIsActive = 1;
    mParticles[i].iWorldX = x;
    mParticles[i].iWorldY = y;
    mParticles[i].iSpeedX = sx;
    mParticles[i].iSpeedY = sy;
}

static void chopGenerateBlockIfNeeded(void)
{
    int i=0;
    int DistSpeedX = iPlayerSpeedX * 5;
    if(DistSpeedX<200) DistSpeedX = 200;

    while(i < NUMBER_OF_BLOCKS)
    {
        if(!mBlocks[i].bIsActive)
        {
            int iX,iY,sX,sY;

            iX = iLastBlockPlacedPosX + (350-DistSpeedX);
            sX = blockw;

            iY = iR(0,iScreenY);
            sY = blockh + iR(1,blockh/3);

            chopAddBlock(iX,iY,sX,sY,i);
        }

        i++;
    }

}

static int chopBlockCollideWithPlayer(struct CBlock *mBlock)
{
    int px = iPlayerPosX;
    int py = iPlayerPosY;

    int x = mBlock->iWorldX-17;
    int y = mBlock->iWorldY-11;

    int x2 = x + mBlock->iSizeX+17;
    int y2 = y + mBlock->iSizeY+11;

    if(px>x && px<x2)
    {
        if(py>y && py<y2)
        {
            return 1;
        }
    }

    return 0;
}

static int chopBlockOffscreen(struct CBlock *mBlock)
{
    if(mBlock->iWorldX + mBlock->iSizeX < iCameraPosX)
        return 1;
    else
        return 0;
}

static int chopParticleOffscreen(struct CParticle *mParticle)
{
    if (mParticle->iWorldX < iCameraPosX || mParticle->iWorldY < 0 ||
        mParticle->iWorldY > iScreenY || mParticle->iWorldX > iCameraPosX +
        iScreenX)
    {
        return 1;
    }
    else
        return 0;
}

static void chopKillPlayer(void)
{
    int i, button;

    for (i = 0; i < NUMBER_OF_PARTICLES; i++) {
        mParticles[i].bIsActive = 0;
        chopAddParticle(iPlayerPosX + iR(0,20), iPlayerPosY + iR(0,20),
                        iR(-2,2), iR(-2,2));
    }

    iPlayerAlive--;

    if (iPlayerAlive == 0) {
        rb->lcd_set_drawmode(DRMODE_FG);
#if LCD_DEPTH >= 2
        rb->lcd_set_foreground(LCD_LIGHTGRAY);
#endif
        rb->splash(HZ, true, "Game Over");

        if (score > highscore) {
            char scoretext[30];
            highscore = score;
            rb->snprintf(scoretext, sizeof(scoretext), "New High Score: %d",
                         highscore);
            rb->splash(HZ*2, true, scoretext);
        }

        rb->splash(HZ/4, true, "Press " ACTIONTEXT " to continue");
        rb->lcd_update();

        rb->lcd_set_drawmode(DRMODE_SOLID);

        while (true) {
            button = rb->button_get(true);
            if (button == ACTION
#ifdef ACTION2
                || button == ACTION2
#endif
                ) {
                while (true) {
                    button = rb->button_get(true);
                    if (button == (ACTION | BUTTON_REL)
#ifdef ACTION2
                        || button == (ACTION2 | BUTTON_REL)
#endif
                       ) {
                        chopper_load(true);
                        return;
                    }
                }
            }
        }

    } else
        chopper_load(false);

}

static void chopDrawTheWorld(void)
{
    int i=0;

    while(i < NUMBER_OF_BLOCKS)
    {
        if(mBlocks[i].bIsActive)
        {
            if(chopBlockOffscreen(&mBlocks[i]) == 1)
                mBlocks[i].bIsActive = 0;
            else
                chopDrawBlock(&mBlocks[i]);
        }

        i++;
    }

    i=0;

    while(i < NUMBER_OF_PARTICLES)
    {
        if(mParticles[i].bIsActive)
        {
            if(chopParticleOffscreen(&mParticles[i]) == 1)
                mParticles[i].bIsActive = 0;
            else
                chopDrawParticle(&mParticles[i]);
        }

        i++;
    }

    chopRenderTerrain(&mGround);
    chopRenderTerrain(&mRoof);

}

static void chopDrawParticle(struct CParticle *mParticle)
{

    int iPosX = (mParticle->iWorldX - iCameraPosX);
    int iPosY = (mParticle->iWorldY);
#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(192,192,192));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_LIGHTGRAY);
#endif
    rb->lcd_fillrect(iPosX, iPosY, 3, 3);

}

static void chopDrawScene(void)
{
    char s[30];
    int w;
#if LCD_DEPTH > 2
    rb->lcd_set_background(LCD_BLACK);
#elif LCD_DEPTH == 2
    rb->lcd_set_background(LCD_WHITE);
#endif
    rb->lcd_clear_display();

    chopDrawTheWorld();
    chopDrawPlayer(iPlayerPosX - iCameraPosX, iPlayerPosY);

    score = -20 + iPlayerPosX/3;
    rb->lcd_set_drawmode(DRMODE_FG);
#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_BLACK);
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_WHITE);
#endif
    rb->snprintf(s, sizeof(s), "Distance: %d", score);
    rb->lcd_putsxy(2, 2, s);
    rb->snprintf(s, sizeof(s), "Best: %d", highscore);
    rb->lcd_getstringsize(s, &w, NULL);
    rb->lcd_putsxy(LCD_WIDTH - 2 - w, 2, s);
    rb->lcd_set_drawmode(DRMODE_SOLID);

    rb->lcd_update();
}

static int chopMenu(int menunum)
{
    int m;
    int result;
    int res = 0;
    bool menu_quit = false;

    static const struct menu_item items[] = {
        { "Start New Game", NULL },
        { "Resume Game", NULL },
        { "Level", NULL },
        { "Quit", NULL },
    };

    static const struct opt_items levels[2] = {
        { "Normal", -1 },
        { "Steep", -1 },
    };

#ifdef HAVE_LCD_COLOR
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_set_background(LCD_BLACK);
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_set_background(LCD_WHITE);
#endif

    rb->lcd_clear_display();

    m = rb->menu_init(items, sizeof(items) / sizeof(*items),
                      NULL, NULL, NULL, NULL);

    while (!menu_quit) {
        result=rb->menu_show(m);
        switch (result)
        {
            case 0:     /* Start New Game */
                menu_quit=true;
                chopper_load(true);
                res = -1;
                break;
            case 1:     /* Resume Game */
                if(menunum==1) {
                    menu_quit=true;
                    res = -1;
                } else if(menunum==0){
                    rb->splash(HZ, true, "No game to resume");
                }
                break;
            case 2:
                rb->set_option("Level", &iLevelMode, INT, levels, 2, NULL);
                break;
            case 3:
                menu_quit=true;
                res = PLUGIN_OK;
                break;
            case MENU_ATTACHED_USB:
                menu_quit=true;
                res = PLUGIN_USB_CONNECTED;
                break;
        }
    }
    rb->lcd_clear_display();
    rb->menu_exit(m);
    return res;
}

static int chopGameLoop(void)
{
    int move_button, ret;
    bool exit=false;
    int end, i=0, bdelay=0, last_button=BUTTON_NONE;

    if (chopUpdateTerrainRecycling(&mGround) == 1)
        /* mirror the sky if we've changed the ground */
        chopCopyTerrain(&mGround, &mRoof, 0, -( iScreenY * 0.75));

    ret = chopMenu(0);
    if (ret != -1)
        return PLUGIN_OK;

    chopDrawScene();

    while (!exit) {

        end = *rb->current_tick + (CYCLETIME * HZ) / 1000;

        if(chopUpdateTerrainRecycling(&mGround) == 1)
            /* mirror the sky if we've changed the ground */
            chopCopyTerrain(&mGround, &mRoof, 0, -( iScreenY * 0.75));

        iRotorOffset = iR(-1,1);

        /* We need to have this here so particles move when we're dead */

        for (i=0; i < NUMBER_OF_PARTICLES; i++)
            if(mParticles[i].bIsActive == 1)
            {
                mParticles[i].iWorldX += mParticles[i].iSpeedX;
                mParticles[i].iWorldY += mParticles[i].iSpeedY;
            }

        /* Redraw the main window: */
        chopDrawScene();


        iGravityTimerCountdown--;

        if(iGravityTimerCountdown <= 0)
        {
            iGravityTimerCountdown = 3;
            chopAddParticle(iPlayerPosX, iPlayerPosY+5, 0, 0);
        }

        if(iLevelMode == LEVEL_MODE_NORMAL)
            chopGenerateBlockIfNeeded();


        move_button=rb->button_status();
        if (rb->button_get(false) == QUIT) {
            ret = chopMenu(1);
            if (ret != -1)
                return PLUGIN_OK;
            bdelay = 0;
            last_button = BUTTON_NONE;
            move_button = BUTTON_NONE;
        }

        switch (move_button) {
            case ACTION:
#ifdef ACTION2
            case ACTION2:
#endif
                if (last_button != ACTION
#ifdef ACTION2
                    && last_button != ACTION2
#endif
                   )
                    bdelay = -2;
                if (bdelay == 0)
                    iPlayerSpeedY = -3;
                break;

            default:
                if (last_button == ACTION
#ifdef ACTION2
                    || last_button == ACTION2
#endif
                   )
                    bdelay = 3;
                if (bdelay == 0)
                    iPlayerSpeedY = 4;

                if (rb->default_event_handler(move_button) == SYS_USB_CONNECTED)
                    return PLUGIN_USB_CONNECTED;
                break;
        }
        last_button = move_button;

        if (bdelay < 0) {
            iPlayerSpeedY = bdelay;
            bdelay++;
        } else if (bdelay > 0) {
            iPlayerSpeedY = bdelay;
            bdelay--;
        }

        iCameraPosX = iPlayerPosX - 25;
        iPlayerPosX += iPlayerSpeedX;
        iPlayerPosY += iPlayerSpeedY;

        chopCounter++;
        /* increase speed as we go along */
        if (chopCounter == 100){
            iPlayerSpeedX++;
            chopCounter=0;
        }

        if (iPlayerPosY > iScreenY-10 || iPlayerPosY < -5 ||
           chopPointInTerrain(&mGround, iPlayerPosX, iPlayerPosY + 10, 0) ||
           chopPointInTerrain(&mRoof, iPlayerPosX ,iPlayerPosY, 1))
        {
           chopKillPlayer();
           chopDrawScene();
           ret = chopMenu(0);
           if (ret != -1)
               return ret;
        }

        for (i=0; i < NUMBER_OF_BLOCKS; i++)
            if(mBlocks[i].bIsActive == 1)
                if(chopBlockCollideWithPlayer(&mBlocks[i])) {
                    chopKillPlayer();
                    chopDrawScene();
                    ret = chopMenu(0);
                    if (ret != -1)
                        return ret;
                }

        if (end > *rb->current_tick)
            rb->sleep(end-*rb->current_tick);
        else
            rb->yield();

    }
    return PLUGIN_OK;
}

static void chopDrawBlock(struct CBlock *mBlock)
{
    int iPosX = (mBlock->iWorldX - iCameraPosX);
    int iPosY = (mBlock->iWorldY);
#if LCD_DEPTH > 2
    rb->lcd_set_foreground(LCD_RGBPACK(100,255,100));
#elif LCD_DEPTH == 2
    rb->lcd_set_foreground(LCD_BLACK);
#endif
    rb->lcd_fillrect(iPosX, iPosY, mBlock->iSizeX,
                     mBlock->iSizeY);
}


static void chopRenderTerrain(struct CTerrain *ter)
{

    int i=1;

    int oldx=0;

    int ay=0;
    if(ter->mNodes[0].y < LCD_HEIGHT/2)
        ay=0;
    else
        ay=LCD_HEIGHT;

    while(i < ter->iNodesCount && oldx < LCD_WIDTH)
    {

        int x = ter->mNodes[i-1].x - iCameraPosX;
        int y = ter->mNodes[i-1].y;

        int x2 = ter->mNodes[i].x - iCameraPosX;
        int y2 = ter->mNodes[i].y;
#if LCD_DEPTH > 2
        rb->lcd_set_foreground(LCD_RGBPACK(100,255,100));
#elif LCD_DEPTH == 2
        rb->lcd_set_foreground(LCD_DARKGRAY);
#endif

        rb->lcd_drawline(x, y, x2, y2);
        xlcd_filltriangle(x, y, x2, y2, x2, ay);
        xlcd_filltriangle(x, ay, x2, y2, x2, ay);
        if (ay == 0)
            xlcd_filltriangle(x, ay, x, y, x2, y2 / 2);
        else
            xlcd_filltriangle(x, ay, x, y, x2, LCD_HEIGHT - (LCD_HEIGHT - y2) / 2);

        oldx = x;
        i++;

    }

}

void chopper_load(bool newgame)
{

    int i;
    int g;

    if (newgame) {
        iScreenX = LCD_WIDTH;
        iScreenY = LCD_HEIGHT;
        blockh = 0.2*iScreenY;
        blockw = 0.04*iScreenX;
        iPlayerAlive = 1;
        score = 0;
    }
    iRotorOffset = 0;
    iPlayerPosX = 60;
    iPlayerPosY = iScreenY * 0.4;
    iLastBlockPlacedPosX = 0;
    iGravityTimerCountdown = 2;
    chopCounter = 0;
    iPlayerSpeedX = 3;
    iPlayerSpeedY = 0;
    iCameraPosX = 30;

    for (i=0; i < NUMBER_OF_PARTICLES; i++)
        mParticles[i].bIsActive = 0;

    for (i=0; i < NUMBER_OF_BLOCKS; i++)
        mBlocks[i].bIsActive = 0;

    g = iScreenY - 10;
    chopClearTerrain(&mGround);

    for (i=0; i < MAX_TERRAIN_NODES; i++)
        chopAddTerrainNode(&mGround,i * 30,g - iR(0,20));

    if (chopUpdateTerrainRecycling(&mGround) == 1)
        /* mirror the sky if we've changed the ground */
        chopCopyTerrain(&mGround, &mRoof, 0, -( iScreenY * 0.75));

    iLevelMode = LEVEL_MODE_NORMAL;
    if (iLevelMode == LEVEL_MODE_NORMAL)
        /* make it a bit more exciting, cause it's easy terrain... */
        iPlayerSpeedX *= 2;
}

/* this is the plugin entry point */
enum plugin_status plugin_start(struct plugin_api* api, void* parameter)
{
    (void)parameter;
    rb = api;
    int ret;

    rb->lcd_setfont(FONT_SYSFIXED);
#if LCD_DEPTH > 1
    rb->lcd_set_backdrop(NULL);
#endif
#ifdef HAVE_LCD_COLOR
    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_set_foreground(LCD_WHITE);
#endif

    /* Permanently enable the backlight (unless the user has turned it off) */
    if (rb->global_settings->backlight_timeout > 0)
        rb->backlight_set_timeout(1);

    rb->srand( *rb->current_tick );

    xlcd_init(rb);
    configfile_init(rb);
    configfile_load(CFG_FILE, config, 1, 0);

    chopper_load(true);
    ret = chopGameLoop();

    configfile_save(CFG_FILE, config, 1, 0);

    /* Restore user's original backlight setting */
    rb->lcd_setfont(FONT_UI);
    rb->backlight_set_timeout(rb->global_settings->backlight_timeout);

    return ret;
}
