<!--
TeamSpeak 3 Channel Infoframe Template
Copyright (c) TeamSpeak Systems GmbH
 
The replaceable variables are embedded in "%%" like %%CHANNEL_NAME%%. At this time you can also use 
%%?CHANNEL_NAME%% (note the questionmark), which is a tiny "if"- query. Use it, to remove the whole 
line, if a variable is empty or just "0".

Templates can be placed in "styles/" for default theme or in a sub folder named like an available 
theme (e.g. "styles/bluesky/"). Be aware that this template will not automaticly be translated when 
displayed.

Predefined values have to be inside the html comment-tag to make sure that they will be parsed
before the replacing begins!

Options (remove the "#" to enable):
#%%IMAGES_MAX_WIDTH%%256
#%%IMAGES_MAX_HEIGHT%%256

Replacable variables for channels:
CHANNEL_NAME
CHANNEL_NAME_PLAIN
CHANNEL_ID
CHANNEL_ICON
CHANNEL_ICON_SCALED
CHANNEL_TOPIC
CHANNEL_MAXCLIENTS
CHANNEL_MAXFAMILYCLIENTS
CHANNEL_NEEDED_TALK_POWER
CHANNEL_MODERATED_ICON
CHANNEL_ORDER
CHANNEL_CODEC
CHANNEL_CODEC_BITRATE
CHANNEL_FLAGS
CHANNEL_FLAG_PERMANENT
CHANNEL_FLAG_SEMI_PERMANENT
CHANNEL_FLAG_TEMPORARY
CHANNEL_FLAG_PASSWORD
CHANNEL_FLAG_DEFAULT
CHANNEL_FLAG_NORMAL
CHANNEL_FLAG_SPACER
CHANNEL_FLAG_FORCED_SILENCE
CHANNEL_SUBSCRIPTION
CHANNEL_CLIENTS_COUNT
CHANNEL_VOICE_DATA_ENCRYPTED
CHANNEL_VOICE_DATA_ENCRYPTED_FLAG
CHANNEL_DESCRIPTION
TEMP_CHANNEL_DELETE_DELAY
TEMP_CHANNEL_TIME_TO_DELETE
PLUGIN_INFO_DATA
IMAGES_MAX_WIDTH
IMAGES_MAX_HEIGHT
OPEN_FILE_BROWSER
FILE_BROWSER_ICON
-->

<!-- begin dummy container including tooltip (single line html table) -->

<div class="InfoFrame" title="<table><tr><td><b><nobr>%%TR_CHANNEL_SUBSCRIPTION%%:&nbsp;</nobr></b></td><td><nobr>%%CHANNEL_SUBSCRIPTION%%</nobr></td></tr><tr><td><b><nobr>%%TR_CHANNEL_NEEDED_TALK_POWER%%:&nbsp;</nobr></b></td><td><nobr>%%CHANNEL_NEEDED_TALK_POWER%%</nobr></td></tr><tr><td colspan=2><hr /></td></tr><tr><td><b><nobr>%%TR_CHANNEL_CODEC_QUALITY%%:&nbsp;</nobr></b></td><td><nobr>%%CHANNEL_CODEC_QUALITY%% (%%TR_CHANNEL_CODEC_BITRATE%%: %%CHANNEL_CODEC_BITRATE%%/s)</nobr></td></tr><tr><td><b><nobr>%%TR_CHANNEL_VOICE_DATA_ENCRYPTED%%:&nbsp;</nobr></b></td><td><nobr>%%CHANNEL_VOICE_DATA_ENCRYPTED%%</nobr></td></tr></table>">

  <!-- header -->

  <div class="InfoFrame_Header" dir="LTR">
    %%CHANNEL_ICON_SCALED%%
    <a href="channelid://%%CHANNEL_ID%%" class="InfoFrame_NameLink">%%CHANNEL_NAME%%</a>
    <div class="InfoFrame_Meta">%%CHANNEL_CLIENTS_COUNT%% / %%CHANNEL_FLAG_MAXCLIENTS%%</div>
  </div>

  <hr />

  <table class="InfoFrame_Table" width="100%" cellspacing="0" cellpadding="0">
    <tr><td class="InfoFrame_Label" width="78">%%?TR_CHANNEL_TOPIC%%</td><td class="InfoFrame_Value">%%?CHANNEL_TOPIC%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%TR_CHANNEL_CODEC%%</td><td class="InfoFrame_Value">%%CHANNEL_CODEC%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%?TR_CHANNEL_FLAGS%%</td><td class="InfoFrame_Value">%%?CHANNEL_FLAGS%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%?TR_TEMP_CHANNEL_TIME_TO_DELETE%%</td><td class="InfoFrame_Value InfoFrame_Warn">%%?TEMP_CHANNEL_TIME_TO_DELETE%%</td></tr>
    <tr><td class="InfoFrame_Label" width="78">%%?TR_CHANNEL_MODERATED%%</td><td class="InfoFrame_Value">%%CHANNEL_MODERATED_ICON%%&nbsp;&nbsp;%%TR_YES%%</td></tr>
    %%?PLUGIN_INFO_DATA%%
  </table>

  %%??CHANNEL_DESCRIPTION%%<hr />
  %%??CHANNEL_DESCRIPTION%%<div class="InfoFrame_Section">%%?TR_CHANNEL_DESCRIPTION%%</div>
  %%??CHANNEL_DESCRIPTION%%<div class="InfoFrame_Body">%%?CHANNEL_DESCRIPTION%%</div>

</div>
