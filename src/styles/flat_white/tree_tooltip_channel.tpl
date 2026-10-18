<!--
TeamSpeak 3 Channel Tooltip Template (EN) 
Copyright (c) TeamSpeak Systems GmbH
 
The replaceable variables are embedded in "%%" like %%CHANNEL_NAME%%. At this time you can also use 
%%?CHANNEL_NAME%% (note the questionmark), which is a tiny "if"- query. Use it, to remove the whole 
line, if a variable is empty or just "0".

Templates can be placed in "styles/" for default theme or in a sub folder named like an available 
theme (e.g. "styles/bluesky/"). Be aware that this template will not automaticly be translated when 
displayed.

Predefined values have to be inside the html comment-tag to make sure that they will be parsed
before the replacing begins! Remove the "#" to enable.

#%%IMAGES_MAX_WIDTH%%256
#%%IMAGES_MAX_HEIGHT%%256

Replacable variables for channels:
CHANNEL_NAME
CHANNEL_ID
CHANNEL_TOPIC
CHANNEL_MAXCLIENTS
CHANNEL_MAXFAMILYCLIENTS
CHANNEL_NEEDED_TALK_POWER
CHANNEL_ORDER
CHANNEL_CODEC
CHANNEL_CODEC_BITRATE
CHANNEL_FLAGS
CHANNEL_SUBSCRIPTION
CHANNEL_CLIENTS_COUNT
CHANNEL_VOICE_DATA_ENCRYPTED
CHANNEL_VOICE_DATA_ENCRYPTED_FLAG
CHANNEL_DESCRIPTION
CHANNEL_ICON
PLUGIN_INFO_DATA
IMAGES_MAX_WIDTH
TEMP_CHANNEL_DELETE_DELAY
TEMP_CHANNEL_TIME_TO_DELETE
-->

<style type="text/css">
table#info {
	border-collapse: collapse;
	border-spacing: 0px;
}
td {
	padding: 0px 4px 0px 1px;
	white-space: nowrap;
}
td.label {
	font-weight: bold;
}
.blue {
	color: #0F766E;
}
.red {
	color: #D1453B;
}
</style>

<table id="info">
  <tr><td class="label">%%TR_CHANNEL_NAME%%:</td><td class="blue">%%CHANNEL_NAME%%</td></tr>
  <tr><td class="label">%%?TR_CHANNEL_TOPIC%%:</td><td>%%?CHANNEL_TOPIC%%</td></tr>
  <tr><td class="label">%%TR_CHANNEL_CODEC%%:</td><td>%%CHANNEL_CODEC%%</td></tr>
  <tr><td class="label">%%TR_CHANNEL_CODEC_QUALITY%%:</td><td>%%CHANNEL_CODEC_QUALITY%%</td></tr>
  <tr><td class="label">%%?TR_CHANNEL_FLAGS%%:</td><td class="blue">%%?CHANNEL_FLAGS%%</td></tr>
  <tr><td class="label">%%?TR_CHANNEL_CLIENTS_COUNT%%:</td><td>%%?CHANNEL_CLIENTS_COUNT%% / %%CHANNEL_FLAG_MAXCLIENTS%%</td></tr>
  <tr><td class="label">%%?TR_CHANNEL_NEEDED_TALK_POWER%%:</td><td class="red">%%?CHANNEL_NEEDED_TALK_POWER%%</td></tr>
  <tr><td class="label">%%TR_CHANNEL_SUBSCRIPTION%%:</td><td>%%CHANNEL_SUBSCRIPTION%%</td></tr>
  <tr><td class="label">%%TR_CHANNEL_VOICE_DATA_ENCRYPTED%%:</td><td>%%CHANNEL_VOICE_DATA_ENCRYPTED%%</td></tr>
  %%?PLUGIN_INFO_DATA%%
</table>

<p style="margin-top: 4px; color:#D1453B;">%%?TEMP_CHANNEL_TIME_TO_DELETE%%</p>
