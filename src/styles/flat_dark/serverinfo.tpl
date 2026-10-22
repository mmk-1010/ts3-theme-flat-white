<!--
TeamSpeak 3 Server Infoframe Template
Copyright (c) TeamSpeak Systems GmbH
 
The replaceable variables are embedded in "%%" like %%SERVER_NAME%%. At this time you can also use 
%%?SERVER_NAME%% (note the questionmark), which is a tiny "if"- query. Use it, to remove the whole 
line, if a variable is empty or just "0".

Templates can be placed in "styles/" for default theme or in a sub folder named like an available 
theme (e.g. "styles/bluesky/"). Be aware that this template will not automaticly be translated when 
displayed.

Replacable variables for server:
SERVER_NAME
SERVER_WELCOMEMESSAGE
SERVER_HOSTMESSAGE
SERVER_ID
SERVER_UNIQUE_ID
SERVER_VERSION
SERVER_PLATFORM
SERVER_CLIENTS_ONLINE
SERVER_QUERYCLIENTS_ONLINE
SERVER_CHANNELS_ONLINE
SERVER_UPTIME
SERVER_CLIENT_CONNECTIONS
SERVER_QUERY_CLIENT_CONNECTIONS
SERVER_ADDRESS
SERVER_PORT
SERVER_ICON
SERVER_ICON_SCALED
SERVER_LICENSE
PLUGIN_INFO_DATA
SERVER_MAXCLIENTS
SERVER_NO_RESERVED_SLOTS
SERVER_RESERVED_SLOTS
SERVER_REFRESH_INACTIVE
SERVER_REFRESH_ACTIVE
SERVER_REFRESH_ICON
-->

<!-- begin dummy container including tooltip (single line html table) -->

<div class="InfoFrame" title="<table><tr><td><b><nobr>%%TR_SERVER_CLIENTS_ONLINE%%:&nbsp;</nobr></b></td><td><nobr>%%SERVER_CLIENTS_ONLINE%% / %%SERVER_MAXCLIENTS%%</nobr></td></tr><tr><td><b><nobr>%%TR_SERVER_CLIENT_CONNECTIONS%%:&nbsp;</nobr></b></td><td><nobr>%%SERVER_CLIENT_CONNECTIONS%%</nobr></td></tr><tr><td colspan=2><hr /></td></tr><tr><td><b><nobr>%%TR_SERVER_VERSION%%:&nbsp;</nobr></b></td><td><nobr>%%SERVER_VERSION%%</nobr></td></tr></table>">

  <!-- header -->

  <div class="InfoFrame_Header" dir="LTR">
    %%SERVER_ICON_SCALED%%
    <a href="channelid://0" class="InfoFrame_NameLink">%%SERVER_NAME%%</a>
    <div class="InfoFrame_Meta">%%SERVER_ADDRESS%%:%%?SERVER_PORT%%</div>
  </div>

  <hr />

  <table class="InfoFrame_Table" width="100%" cellspacing="0" cellpadding="0">
    <tr><td class="InfoFrame_Label" width="78">%%TR_SERVER_NICKNAMES%%</td><td class="InfoFrame_Value">%%?SERVER_NICKNAMES%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%TR_SERVER_VERSION%%</td><td class="InfoFrame_Value">%%SERVER_VERSION_SHORT%% %%SERVER_PLATFORM%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%TR_SERVER_LICENSE%%</td><td class="InfoFrame_Value">%%SERVER_LICENSE%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%TR_SERVER_UPTIME%%</td><td class="InfoFrame_Value">%%SERVER_UPTIME%%</td></tr>
  </table>

  <hr />

  <div class="InfoFrame_Section">%%TR_SERVER_CLIENTS_ONLINE%%</div>
  <table class="InfoFrame_Table" width="100%" cellspacing="0" cellpadding="0">
    <tr>
      <td class="InfoFrame_Value">%%SERVER_CLIENTS_ONLINE%% / %%SERVER_MAXCLIENTS%% %%?SERVER_NO_RESERVED_SLOTS%%</td>
      <td class="InfoFrame_Value">%%SERVER_CLIENTS_ONLINE%% / %%SERVER_MAXCLIENTS%% (<span class="InfoFrame_Warn">-%%?SERVER_RESERVED_SLOTS%% reserved</span>)</td>
    </tr>
    <tr>
      <td class="InfoFrame_Label" width="78">%%TR_SERVER_CHANNELS_ONLINE%%</td>
      <td class="InfoFrame_Value">%%SERVER_CHANNELS_ONLINE%%</td>
    </tr>
    %%?PLUGIN_INFO_DATA%%
  </table>

  <hr />

  <div class="InfoFrame_Meta">
    %%SERVER_REFRESH_ICON%%
    <a class="Inactive" href="%%?SERVER_REFRESH_INACTIVE%%">%%?TR_SERVER_REFRESH_INACTIVE%%</a>
    <a class="Active" href="%%?SERVER_REFRESH_ACTIVE%%">%%?TR_SERVER_REFRESH_ACTIVE%%</a>
  </div>

</div>
