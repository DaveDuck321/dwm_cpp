/* See LICENSE file for copyright and license details. */
#include <array>
#include <string>
#include <vector>

/* appearance */
const unsigned int borderpx  = 1;        /* border pixel of windows */
const unsigned int gappx     = 5;        /* gaps between windows */
const unsigned int snap      = 32;       /* snap pixel */
const int showbar            = 1;        /* 0 means no bar */
const int topbar             = 1;        /* 0 means bottom bar */
const std::vector<std::string> fonts { "monospace:size=10" };
const char dmenufont[]       = "monospace:size=10";
const char col_gray1[]       = "#222222";
const char col_gray2[]       = "#444444";
const char col_gray3[]       = "#bbbbbb";
const char col_gray4[]       = "#eeeeee";
const char col_cyan[]        = "#005577";

const Theme<ColorScheme> colors {
	.normal = {
		.foreground = col_gray3,
		.background = col_gray1,
		.border = col_gray2,
	},
	.selected = {
		.foreground = col_gray4,
		.background = col_cyan,
		.border = col_cyan,
	},
};

/* tagging */
const std::array<std::string, 9> tags { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

const std::array<Rule, 2> rules = {{
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1 },
}};

/* layout(s) */
const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
const int nmaster     = 1;    /* number of clients in master area */
const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */

const std::array<Layout, 3> layouts = {{
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
}};

/* key definitions */
#define MODKEY Mod1Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      []{view(1 << TAG);}}, \
	{ MODKEY|ControlMask,           KEY,      []{toggleview(1 << TAG);}}, \
	{ MODKEY|ShiftMask,             KEY,      []{tag(1 << TAG);}}, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      []{toggletag(1 << TAG);}},

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
char spawnCommandMonitorID[2] = {'0', '\0'}; /* component of dmenurun, manipulated in spawn() */
Command dmenurun = { "dmenu_run", "-m", spawnCommandMonitorID, "-fn", dmenufont, "-nb", col_gray1, "-nf", col_gray3, "-sb", col_cyan, "-sf", col_gray4 };
Command terminal  = { "st" };

void autostart() { spawn(terminal); }

Key keys[] = {
	/* modifier                     key        function  */
	{ MODKEY,                       XK_p,      []{spawn(dmenurun);}},
	{ MODKEY|ShiftMask,             XK_Return, []{spawn(terminal);}},
	{ MODKEY,                       XK_b,      togglebar},
	{ MODKEY,                       XK_j,      []{focusstack(+1);}},
	{ MODKEY,                       XK_k,      []{focusstack(-1);}},
	{ MODKEY,                       XK_i,      []{incnmaster(+1);}},
	{ MODKEY,                       XK_d,      []{incnmaster(-1);}},
	{ MODKEY,                       XK_h,      []{setmfact(-0.05f);}},
	{ MODKEY,                       XK_l,      []{setmfact(+0.05f);}},
	{ MODKEY,                       XK_Return, zoom},
	{ MODKEY,                       XK_Tab,    []{view(0);}},
	{ MODKEY|ShiftMask,             XK_c,      killclient},
	{ MODKEY,                       XK_t,      []{setlayout(&layouts[0]);}},
	{ MODKEY,                       XK_f,      []{setlayout(&layouts[1]);}},
	{ MODKEY,                       XK_m,      []{setlayout(&layouts[2]);}},
	{ MODKEY,                       XK_space,  togglelayout},
	{ MODKEY|ShiftMask,             XK_space,  togglefloating},
	{ MODKEY,                       XK_0,      []{view(~0u);}},
	{ MODKEY|ShiftMask,             XK_0,      []{tag(~0u);}},
	{ MODKEY,                       XK_comma,  []{focusmon(-1);}},
	{ MODKEY,                       XK_period, []{focusmon(+1);}},
	{ MODKEY|ShiftMask,             XK_comma,  []{tagmon(-1);}},
	{ MODKEY|ShiftMask,             XK_period, []{tagmon(+1);}},
	{ MODKEY,                       XK_minus,  []{setgaps(-1);}},
	{ MODKEY,                       XK_equal,  []{setgaps(+1);}},
	{ MODKEY|ShiftMask,             XK_equal,  []{setgaps(0);}},
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ShiftMask,             XK_q,      quit},
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
const std::array<Button, 11> buttons = {{
	/* click                event mask      button          function */
	{ ClkLtSymbol,          0,              Button1,        [](int){togglelayout();}},
	{ ClkLtSymbol,          0,              Button3,        [](int){setlayout(&layouts[2]);}},
	{ ClkWinTitle,          0,              Button2,        [](int){zoom();}},
	{ ClkStatusText,        0,              Button2,        [](int){spawn(terminal);}},
	{ ClkClientWin,         MODKEY,         Button1,        [](int){movemouse();}},
	{ ClkClientWin,         MODKEY,         Button2,        [](int){togglefloating();}},
	{ ClkClientWin,         MODKEY,         Button3,        [](int){resizemouse();}},
	{ ClkTagBar,            0,              Button1,        view },
	{ ClkTagBar,            0,              Button3,        toggleview },
	{ ClkTagBar,            MODKEY,         Button1,        tag },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag },
}};
