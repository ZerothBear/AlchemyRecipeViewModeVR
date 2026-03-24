Scriptname AlchemyRecipeViewVRMCM extends MCM_ConfigBase

Bool _suppressSettingChange = False

String Function GetModName()
    return "Alchemy Helper"
EndFunction

Function EnsureModName()
    If ModName == ""
        ModName = "Alchemy Helper"
    EndIf
EndFunction

Function ClearActionToggles()
    SetModSettingBool("bResetSettings:General", False)
EndFunction

Event OnConfigInit()
    EnsureModName()
    ClearActionToggles()
    ReloadSettings()
EndEvent

Event OnGameReload()
    EnsureModName()
    parent.OnGameReload()
    ClearActionToggles()
    ReloadSettings()
EndEvent

Event OnSettingChange(String a_ID)
    If _suppressSettingChange
        Return
    EndIf

    If a_ID == "bResetSettings:General"
        If GetModSettingBool(a_ID)
            _suppressSettingChange = True

            If ShowMessage("Reset Alchemy Recipe View VR settings to defaults?", True, "$Accept", "$Cancel")
                ApplyDefaultSettings()
            EndIf

            ClearActionToggles()
            _suppressSettingChange = False

            ReloadSettings()
            RefreshMenu()
        EndIf
        Return
    EndIf

    ReloadSettings()
EndEvent

Function ReloadSettings() native
Bool Function GetDefaultBoolSetting(String a_ID) native
Int Function GetDefaultIntSetting(String a_ID) native

Function ApplyDefaultSettings()
    SetModSettingBool("bEnable:General", GetDefaultBoolSetting("bEnable:General"))
    SetModSettingBool("bDebugLogging:General", GetDefaultBoolSetting("bDebugLogging:General"))
    SetModSettingInt("iToggleKey:Controls", GetDefaultIntSetting("iToggleKey:Controls"))
    SetModSettingBool("bShowNavButton:Behavior", GetDefaultBoolSetting("bShowNavButton:Behavior"))
    SetModSettingBool("bBlockCraftWhileEnabled:Behavior", GetDefaultBoolSetting("bBlockCraftWhileEnabled:Behavior"))
EndFunction
