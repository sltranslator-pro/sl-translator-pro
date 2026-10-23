# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SL Translator Pro contributors

param([string]$InstDir)

# Patch commands.xml — add sltranslator toolbar command
$cmdFile = "$InstDir\app_settings\commands.xml"
if (Test-Path $cmdFile) {
    $content = Get-Content -Raw $cmdFile
    if ($content -notmatch 'sltranslator') {
        $inject = @'
  <command name="sltranslator"
           available_in_toybox="true"
           icon="Command_Chat_Icon"
           label_ref="Command_SLTranslator_Label"
           tooltip_ref="Command_SLTranslator_Tooltip"
           execute_function="Floater.ToggleOrBringToFront"
           execute_parameters="prefs_translation"
           is_running_function="Floater.IsOpen"
           is_running_parameters="prefs_translation"
           />
</commands>
'@
        $content = $content -replace '</commands>', $inject
        [System.IO.File]::WriteAllText($cmdFile, $content)
    }
}

# Patch strings.xml — add SL Translator labels
$strFile = "$InstDir\skins\default\xui\en\strings.xml"
if (Test-Path $strFile) {
    $content = Get-Content -Raw $strFile
    if ($content -notmatch 'SLTranslator') {
        $inject = @'
  <string name="Command_SLTranslator_Label">SL Translator</string>
  <string name="Command_SLTranslator_Tooltip">SL Translator Pro - Real-time chat translation</string>
</strings>
'@
        $content = $content -replace '</strings>', $inject
        [System.IO.File]::WriteAllText($strFile, $content)
    }
}

# Patch menu_viewer.xml — add SL Translator top menu
$menuFile = "$InstDir\skins\default\xui\en\menu_viewer.xml"
if (Test-Path $menuFile) {
    $content = Get-Content -Raw $menuFile
    if ($content -notmatch 'SLTranslator') {
        $inject = @'

    <!-- SL Translator Menu -->
    <menu
     create_jump_keys="true"
     label="SL Translator"
     name="SLTranslator"
     tear_off="true"
     visible="true">

        <menu_item_call
         label="Settings..."
         name="SLT_Settings"
         shortcut="control|shift|T">
            <menu_item_call.on_click
             function="Floater.ToggleOrBringToFront"
             parameter="prefs_translation" />
        </menu_item_call>

    </menu>

</menu_bar>
'@
        $content = $content -replace '</menu_bar>', $inject
        [System.IO.File]::WriteAllText($menuFile, $content)
    }
}

# Patch floater_preferences.xml — add SL Translator side tab
$prefFile = "$InstDir\skins\default\xui\en\floater_preferences.xml"
if (Test-Path $prefFile) {
    $content = Get-Content -Raw $prefFile
    if ($content -notmatch 'sltranslator') {
        $inject = @'
        <panel
         class="panel_preference"
         filename="panel_preferences_sltranslator.xml"
         label="SL Translator"
         layout="topleft"
         help_topic="preferences_sltranslator_tab"
         name="sltranslator" />
    </tab_container>
'@
        $content = $content -replace '</tab_container>', $inject
        [System.IO.File]::WriteAllText($prefFile, $content)
    }
}
