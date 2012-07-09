/* Copyright (C) 2012, dark_skeleton (d-rez) <dark.skeleton@gmail.com>
   Copyright (C) 2009-2012, Snares <snares@users.sourceforge.net>
   Copyright (C) 2005-2012, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "../mumble_plugin_win32.h"

static BYTE *posrotptr;
static BYTE *stateptr;
static BYTE *hostptr;

/*
	note that these are just examples of memory values, and may not be updated or correct
	position tuple:		client.dll+0x...  (x,y,z, float)
	orientation tuple:	client.dll+0x...  (v,h float)
	spawn state:        client.dll+0x...  (0 when at main menu, 1 when spectator, 3 when at team selection menu, and 6 or 9 when on a team (or 4,5,8 etc. depending on the team side and gamemode), byte)
	host string: 		client.dll+0x...  (ip:port or loopback:0 when created own server or blank when not ingame)
	ID string:			client.dll+0x... = "Demoman" (13 characters, text - somerhing that lets us know it's TF2 and not for example HL2)
	note that memory addresses in this comment are for example only; the real ones are defined below

	note: the spec_pos console command is rather useful
	note: so is the cl_showpos command
	NOTE: from my experience, using engine.dll module is a rather bad idea since it's very dynamic. And since we can get everything we need from client.dll, it is no longer required :)
*/

static bool calcout(float *pos, float *rot, float *opos, float *front, float *top) {
	float h = rot[0];
	float v = rot[1];

	if ((v < -360.0f) || (v > 360.0f) || (h < -360.0f) || (h > 360.0f))
		return false;

	h *= static_cast<float>(M_PI / 180.0f);
	v *= static_cast<float>(M_PI / 180.0f);

	// Seems TF2 is in inches. INCHES?!?
	opos[0] = pos[0] / 39.37f;
	opos[1] = pos[2] / 39.37f;
	opos[2] = pos[1] / 39.37f;

	// h rotation angle up-down, positive in down direction starting from x-axis
	// v rotation angle left-right, positive to the left starting from x-axis
	front[0] = cosf(h) * cosf(v);
	front[1] = -sinf(h);
	front[2] = cosf(h) * sinf(v);
	// sin(h - 1/2pi) = -cos(h) and cos(h - 1/2pi) = sin(h)
	top[0] = sinf(h) * cosf(v);
	top[1] = cosf(h);
	top[2] = sinf(h) * sinf(v);

	return true;
}

static int fetch(float *avatar_pos, float *avatar_front, float *avatar_top, float *camera_pos, float *camera_front, float *camera_top, std::string &context, std::wstring &/*identity*/) {
	for (int i=0;i<3;i++)
		avatar_pos[i] = avatar_front[i] = avatar_top[i] = camera_pos[i] = camera_front[i] = camera_top[i] = 0.0f;

	bool ok;
	float posrot[5];
	char state;
	char chHostStr[21]; // We just need 21 [xxx.xxx.xxx.xxx:yyyyy]

	ok = peekProc(posrotptr, posrot) &&
	     peekProc(stateptr, state) &&
	     peekProc(hostptr, chHostStr);

	if (!ok)
		return false;

	std::string sHost(chHostStr);

	// Possible values of chHostStr:
	//	xxx.yyy.zzz.aaa:ccccc (or shorter, e.g. x.y.z.a:cc - but it doesn't really change anything)
	//	loopback:0 (when a local server is started)
	if (!sHost.empty())
	{
		if (sHost.find("loopback") == std::string::npos)
		{
			std::ostringstream newcontext;
			newcontext << "{\"ipport\": \"" << sHost << "\"}";
			context = newcontext.str();
		}
	}

	//TODO: Implement identity

	// Check to see if you are in a server and spawned
	if (state == 0 || state == 1 || state == 3) {
		if (state == 0) context = std::string(""); // clear context if not connected to server
		return true; // Deactivate plugin
	}

	ok = calcout(posrot, posrot+3, avatar_pos, avatar_front, avatar_top);
	if (ok) {
		for (int i=0;i<3;++i) {
			camera_pos[i] = avatar_pos[i];
			camera_front[i] = avatar_front[i];
			camera_top[i] = avatar_top[i];
		}
		return true;
	}

	return false;
}

static int trylock(const std::multimap<std::wstring, unsigned long long int> &pids) {
	posrotptr = stateptr = hostptr = NULL;

	if (! initialize(pids, L"hl2.exe", L"client.dll"))
		return false;

	// Remember addresses for later
	posrotptr = pModule + 0x8F9334;
	stateptr = pModule + 0x869034;
	hostptr = pModule + 0x918044;

	// Gamecheck - check if we're looking at the right game
	char sMagic[7];
	if (!peekProc(pModule + 0x882CE2, sMagic) || strncmp("Demoman", sMagic, sizeof(sMagic))!=0)
		return false;

	// Check if we can get meaningful data from it
	float apos[3], afront[3], atop[3];
	float cpos[3], cfront[3], ctop[3];
	std::wstring sidentity;
	std::string scontext;

	if (fetch(apos, afront, atop, cpos, cfront, ctop, scontext, sidentity)) {
		return true;
	} else {
		generic_unlock();
		return false;
	}
}

static const std::wstring longdesc() {
	return std::wstring(L"Supports TF2 build 4934 with context. No identity support yet.");
}

static std::wstring description(L"Team Fortress 2 (Build 4934)");
static std::wstring shortname(L"Team Fortress 2");

static int trylock1() {
	return trylock(std::multimap<std::wstring, unsigned long long int>());
}

static MumblePlugin tf2plug = {
	MUMBLE_PLUGIN_MAGIC,
	description,
	shortname,
	NULL,
	NULL,
	trylock1,
	generic_unlock,
	longdesc,
	fetch
};

static MumblePlugin2 tf2plug2 = {
	MUMBLE_PLUGIN_MAGIC_2,
	MUMBLE_PLUGIN_VERSION,
	trylock
};

extern "C" __declspec(dllexport) MumblePlugin *getMumblePlugin() {
	return &tf2plug;
}

extern "C" __declspec(dllexport) MumblePlugin2 *getMumblePlugin2() {
	return &tf2plug2;
}
