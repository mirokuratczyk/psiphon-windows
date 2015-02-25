/*
 * Copyright (c) 2015, Psiphon Inc.
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "resource.h"
#include "psiclient.h"
#include "usersettings.h"
#include "utilities.h"
#include "htmldlg.h"
#include "coretransport.h"
#include "vpntransport.h"


#define NULL_PORT                       0
#define MAX_PORT                        0xFFFF

#define SPLIT_TUNNEL_NAME               "SplitTunnel"
#define SPLIT_TUNNEL_DEFAULT            FALSE

#define TRANSPORT_NAME                  "Transport"
// TODO: Don't hardcode transport names? Or get rid of transport registry (since the dynamic-ness is gone anyway).
#define TRANSPORT_DEFAULT               CORE_TRANSPORT_PROTOCOL_NAME
#define TRANSPORT_VPN                   VPN_TRANSPORT_PROTOCOL_NAME

#define HTTP_PROXY_PORT_NAME            "LocalHTTPProxyPort"
#define HTTP_PROXY_PORT_DEFAULT         NULL_PORT
#define SOCKS_PROXY_PORT_NAME           "LocalSOCKSProxyPort"
#define SOCKS_PROXY_PORT_DEFAULT        NULL_PORT

#define EGRESS_REGION_NAME              "EgressRegion"
#define EGRESS_REGION_DEFAULT           ""

#define SKIP_BROWSER_NAME               "SkipBrowser"
#define SKIP_BROWSER_DEFAULT            FALSE

#define SKIP_PROXY_SETTINGS_NAME        "SkipProxySettings"
#define SKIP_PROXY_SETTINGS_DEFAULT     FALSE

#define SKIP_UPSTREAM_PROXY_NAME        "SSHParentProxySkip"
#define SKIP_UPSTREAM_PROXY_DEFAULT     FALSE

#define UPSTREAM_PROXY_TYPE_NAME        "SSHParentProxyType"
#define UPSTREAM_PROXY_TYPE_DEFAULT     "https"

#define UPSTREAM_PROXY_HOSTNAME_NAME    "SSHParentProxyHostname"
#define UPSTREAM_PROXY_HOSTNAME_DEFAULT ""

#define UPSTREAM_PROXY_PORT_NAME        "SSHParentProxyPort"
#define UPSTREAM_PROXY_PORT_DEFAULT     NULL_PORT

static HANDLE g_registryMutex = CreateMutex(NULL, FALSE, 0);

int GetSettingDword(const string& settingName, int defaultValue, bool writeDefault=false)
{
    AutoMUTEX lock(g_registryMutex);

    DWORD value = 0;

    if (!ReadRegistryDwordValue(settingName, value))
    {
        value = defaultValue;

        if (writeDefault)
        {
            WriteRegistryDwordValue(settingName, value);
        }
    }

    return value;
}

string GetSettingString(const string& settingName, string defaultValue, bool writeDefault=false)
{
    AutoMUTEX lock(g_registryMutex);

    string value;

    if (!ReadRegistryStringValue(settingName.c_str(), value))
    {
        value = defaultValue;

        if (writeDefault)
        {
            RegistryFailureReason reason = REGISTRY_FAILURE_NO_REASON;
            WriteRegistryStringValue(settingName, value, reason);
        }
    }

    return value;
}

wstring GetSettingString(const string& settingName, wstring defaultValue, bool writeDefault=false)
{
    AutoMUTEX lock(g_registryMutex);

    wstring value;

    if (!ReadRegistryStringValue(settingName.c_str(), value))
    {
        value = defaultValue;

        if (writeDefault)
        {
            RegistryFailureReason reason = REGISTRY_FAILURE_NO_REASON;
            WriteRegistryStringValue(settingName, value, reason);
        }
    }

    return value;
}

void Settings::Initialize()
{
    // Write out the default values for our non-exposed (registry-only) settings. 
    // This is to help users find and modify them.
    (void)GetSettingDword(SKIP_BROWSER_NAME, SKIP_BROWSER_DEFAULT, true);
    (void)GetSettingDword(SKIP_PROXY_SETTINGS_NAME, SKIP_PROXY_SETTINGS_DEFAULT, true);
}

void Settings::ToJson(Json::Value& o_json)
{
    o_json.clear();
    o_json["SplitTunnel"] = Settings::SplitTunnel();
    o_json["VPN"] = (Settings::Transport() == TRANSPORT_VPN);
    o_json["LocalHttpProxyPort"] = Settings::LocalHttpProxyPort();
    o_json["LocalSocksProxyPort"] = Settings::LocalSocksProxyPort();
    o_json["SkipUpstreamProxy"] = Settings::SkipUpstreamProxy();
    o_json["UpstreamProxyHostname"] = Settings::UpstreamProxyHostname();
    o_json["UpstreamProxyPort"] = Settings::UpstreamProxyPort();
    o_json["EgressRegion"] = Settings::EgressRegion();
    o_json["defaults"] = Json::Value();
    o_json["defaults"]["SplitTunnel"] = SPLIT_TUNNEL_DEFAULT;
    o_json["defaults"]["VPN"] = FALSE;
    o_json["defaults"]["LocalHttpProxyPort"] = NULL_PORT;
    o_json["defaults"]["LocalSocksProxyPort"] = NULL_PORT;
    o_json["defaults"]["SkipUpstreamProxy"] = SKIP_UPSTREAM_PROXY_DEFAULT;
    o_json["defaults"]["UpstreamProxyHostname"] = UPSTREAM_PROXY_HOSTNAME_DEFAULT;
    o_json["defaults"]["UpstreamProxyPort"] = NULL_PORT;
    o_json["defaults"]["EgressRegion"] = EGRESS_REGION_DEFAULT;
}

bool Settings::Show(HINSTANCE hInst, HWND hParentWnd)
{
    Json::Value config;
    Settings::ToJson(config);

    stringstream configDataStream;
    Json::FastWriter jsonWriter;
    configDataStream << jsonWriter.write(config);

    tstring result;
    if (ShowHTMLDlg(
        hParentWnd,
        _T("SETTINGS_HTML_RESOURCE"),
        GetLocaleName().c_str(),
        NarrowToTString(configDataStream.str()).c_str(),
        result) != 1)
    {
        // error or user cancelled
        return false;
    }

    Json::Value json;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(WStringToUTF8(result.c_str()), json);
    if (!parsingSuccessful)
    {
        my_print(NOT_SENSITIVE, false, _T("Failed to save settings!"));
        return false;
    }

    bool settingsChanged = false;

    try
    {
        AutoMUTEX lock(g_registryMutex);

        // Note: We're not purposely not bothering to check registry write return values.

        RegistryFailureReason failReason;

        BOOL splitTunnel = json.get("SplitTunnel", 0).asUInt();
        settingsChanged = settingsChanged || !!splitTunnel != Settings::SplitTunnel();
        WriteRegistryDwordValue(SPLIT_TUNNEL_NAME, splitTunnel);

        wstring transport = json.get("VPN", 0).asUInt() ? TRANSPORT_VPN : TRANSPORT_DEFAULT;
        settingsChanged = settingsChanged || transport != Settings::Transport();
        WriteRegistryStringValue(
            TRANSPORT_NAME,
            transport,
            failReason);

        DWORD httpPort = json.get("LocalHttpProxyPort", 0).asUInt();
        settingsChanged = settingsChanged || httpPort != Settings::LocalHttpProxyPort();
        WriteRegistryDwordValue(HTTP_PROXY_PORT_NAME, httpPort);

        DWORD socksPort = json.get("LocalSocksProxyPort", 0).asUInt();
        settingsChanged = settingsChanged || socksPort != Settings::LocalSocksProxyPort();
        WriteRegistryDwordValue(SOCKS_PROXY_PORT_NAME, socksPort);

        string upstreamProxyHostname = json.get("UpstreamProxyHostname", "").asString();
        settingsChanged = settingsChanged || upstreamProxyHostname != Settings::UpstreamProxyHostname();
        WriteRegistryStringValue(
            UPSTREAM_PROXY_HOSTNAME_NAME,
            upstreamProxyHostname,
            failReason);

        DWORD upstreamProxyPort = json.get("UpstreamProxyPort", 0).asUInt();
        settingsChanged = settingsChanged || upstreamProxyPort != Settings::UpstreamProxyPort();
        WriteRegistryDwordValue(UPSTREAM_PROXY_PORT_NAME, upstreamProxyPort);

        BOOL skipUpstreamProxy = json.get("SkipUpstreamProxy", 0).asUInt();
        settingsChanged = settingsChanged || !!skipUpstreamProxy != Settings::SkipUpstreamProxy();
        WriteRegistryDwordValue(SKIP_UPSTREAM_PROXY_NAME, skipUpstreamProxy);

        string egressRegion = json.get("EgressRegion", "").asString();
        settingsChanged = settingsChanged || egressRegion != Settings::EgressRegion();
        WriteRegistryStringValue(
            EGRESS_REGION_NAME,
            egressRegion,
            failReason);
    }
    catch (exception& e)
    {
        my_print(NOT_SENSITIVE, false, _T("%s:%d: JSON parse exception: %S"), __TFUNCTION__, __LINE__, e.what());
    }

    return settingsChanged;
}

bool Settings::SplitTunnel()
{
    // Not yet supported!
    //return !!GetUserSettingDword(SPLIT_TUNNEL_NAME, SPLIT_TUNNEL_DEFAULT);
    return false;
}

tstring Settings::Transport()
{
    tstring transport = GetSettingString(TRANSPORT_NAME, TRANSPORT_DEFAULT);
    if (transport != TRANSPORT_VPN)
    {
        transport = TRANSPORT_DEFAULT;
    }
    return transport;
}

unsigned int Settings::LocalHttpProxyPort()
{
    DWORD port = GetSettingDword(HTTP_PROXY_PORT_NAME, HTTP_PROXY_PORT_DEFAULT);
    if (port > MAX_PORT)
    {
        port = HTTP_PROXY_PORT_DEFAULT;
    }
    return (unsigned int)port;
}

unsigned int Settings::LocalSocksProxyPort()
{
    DWORD port = GetSettingDword(SOCKS_PROXY_PORT_NAME, SOCKS_PROXY_PORT_DEFAULT);
    if (port > MAX_PORT)
    {
        port = SOCKS_PROXY_PORT_DEFAULT;
    }
    return (unsigned int)port;
}

string Settings::UpstreamProxyType()
{
    // We only support one type, but we'll call this to create the registry entry
    (void)GetSettingString(UPSTREAM_PROXY_TYPE_NAME, UPSTREAM_PROXY_TYPE_DEFAULT);
    return UPSTREAM_PROXY_TYPE_DEFAULT;
}

string Settings::UpstreamProxyHostname()
{
    return GetSettingString(UPSTREAM_PROXY_HOSTNAME_NAME, UPSTREAM_PROXY_HOSTNAME_DEFAULT);
}

unsigned int Settings::UpstreamProxyPort()
{
    DWORD port = GetSettingDword(UPSTREAM_PROXY_PORT_NAME, UPSTREAM_PROXY_PORT_DEFAULT);
    if (port > MAX_PORT)
    {
        port = UPSTREAM_PROXY_PORT_DEFAULT;
    }
    return (unsigned int)port;
}

bool Settings::SkipUpstreamProxy()
{
    return !!GetSettingDword(SKIP_UPSTREAM_PROXY_NAME, SKIP_UPSTREAM_PROXY_DEFAULT);
}

string Settings::EgressRegion()
{
    return GetSettingString(EGRESS_REGION_NAME, EGRESS_REGION_DEFAULT);
}

/*
Settings that are not exposed in the UI.
*/

bool Settings::SkipBrowser()
{
    return !!GetSettingDword(SKIP_BROWSER_NAME, SKIP_BROWSER_DEFAULT);
}
    
bool Settings::SkipProxySettings()
{
    return !!GetSettingDword(SKIP_PROXY_SETTINGS_NAME, SKIP_PROXY_SETTINGS_DEFAULT);
}
