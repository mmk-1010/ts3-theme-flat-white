<!--
TeamSpeak 3 Client Tooltip Template
Copyright (c) TeamSpeak Systems GmbH

The replaceable variables are embedded in "%%" like %%CLIENT_NAME%%. At this time you can 
also use %%?CLIENT_NAME%% (note the questionmark), which is a tiny "if"- query. Use it, to 
remove the whole line, if a variable is empty or just "0".

Templates can be placed in "styles/" for default theme or in a sub folder named like an available 
theme (e.g. "styles/bluesky/"). Be aware that this template will not automaticly be translated when 
displayed.

Predefined values have to be inside the html comment-tag to make sure that they will be parsed
before the replacing begins! Remove the "#" to enable.
-->

<style type="text/css">
table { 
    border-collapse: collapse;
    border-spacing: 0px;
}
td {
    padding: 2px 6px 2px 1px;
	white-space: nowrap;
}
td.noborder {
    padding: 0px;
}
td.label {
    font-weight: bold;
    color: #7C848D;
}
td.avatar {
	padding: 0px 8px 0px 0px;
}
.blue {
    color: #2DD4BF;
}
.red {
    color: #F07167;
}
</style>

<table>
<tr>
<td class="noborder avatar">%%?CLIENT_AVATAR%%</td>
<td class="noborder">
<table width="300">
<tr>
    <td class="label">%%TR_CLIENT_NAME%%:</td>
	<td>
		<img src="%%?CLIENT_COUNTRY_IMAGE%%" alt="" />&nbsp;&nbsp;
		<span class="blue">%%CLIENT_NAME%%</span>
		&nbsp;<span class="blue">[%%?CLIENT_CUSTOM_NICK_NAME%%]</span>
    </td>
</tr>
<tr><td class="label">%%TR_CLIENT_DESCRIPTION%%:</td><td>%%?CLIENT_DESCRIPTION%%</td></tr>
<tr>
    <td class="label">%%TR_CLIENT_CONNECTED_SINCE%%:</td>
    <td>%%CLIENT_CONNECTED_SINCE%%</td>
</tr>
</table>
</td></tr>
</table>
