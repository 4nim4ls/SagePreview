; SagePreview GUI installer -- SagePreview 1.0.0 (unsigned until publisher signs).
; Compiles from the Windows 11 x64 Release staging directory.
; Requires a clean install; does NOT overwrite previous installations.
; Does NOT bypass SmartScreen / Smart App Control. Unsigned until owner signs.
#ifndef StageDir
  #error Supply /DStageDir="C:\path\to\dist\SagePreview-1.0.0"
#endif

#define MyAppName "SagePreview"
#define MyAppVersion "1.0.0"

[Setup]
AppId={{A8A7F621-5B63-43A4-81AF-412AB3AFD394}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName=SagePreview 1.0.0
DefaultDirName={localappdata}\Programs\SagePreview
DisableDirPage=yes
DefaultGroupName=SagePreview
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
OutputDir=..\dist
OutputBaseFilename=SagePreview-1.0.0-win11-x64-UNSIGNED-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\SagePreview.exe
AppComments=Native Windows 11 image thumbnails and Spacebar previews
CloseApplications=no
RestartApplications=no

[Files]
Source: "{#StageDir}\SagePreview.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SagePreviewListener.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SageThumbnailProvider.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SageKraThumbnailProvider.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SagePsdThumbnailProvider.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SageAvifThumbnailProvider.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SageAvifCodecCheck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\synthetic-avif.avif"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ZLIB-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Tasks]
Name: "startup"; Description: "Start SagePreview automatically when I sign in"; Flags: checkedonce

[Icons]
Name: "{autoprograms}\SagePreview"; Filename: "{app}\SagePreview.exe"; Comment: "Choose an image to preview"

[Run]
Filename: "{app}\SagePreviewListener.exe"; Description: "Launch SagePreview listener now"; Flags: nowait postinstall skipifsilent

[Code]
const
  ThumbCategory = '{E357FCCD-A995-4576-B01F-234630154E96}';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function Same(const A, B: String): Boolean;
begin
  Result := CompareText(A, B) = 0;
end;

function BindingKey(const Extension: String): String;
begin
  Result := 'Software\Classes\' + Extension + '\shellex\' + ThumbCategory;
end;

function ClassBase(const Clsid: String): String;
begin
  Result := 'Software\Classes\CLSID\' + Clsid;
end;

function OwnServer(const Clsid, DllName: String): Boolean;
var
  Existing: String;
begin
  Result := RegQueryStringValue(HKCU, ClassBase(Clsid) + '\InprocServer32', '', Existing)
    and Same(Existing, ExpandConstant('{app}\' + DllName));
end;

function CheckClassConflict(const Clsid: String): Boolean;
begin
  {A clean install cannot claim an existing CLSID, even an incomplete one.}
  Result := not RegKeyExists(HKCU, ClassBase(Clsid));
end;

function CheckBindingConflict(const Extension, Clsid: String): Boolean;
var
  Existing: String;
begin
  Result := True;
  {Other programs' handlers are retained. Our own stale direct bindings block setup.}
  if RegQueryStringValue(HKCR, Extension + '\shellex\' + ThumbCategory, '', Existing) then
  begin
    if Same(Existing, Clsid) then
      Result := False;
  end;
  if RegQueryStringValue(HKCU, BindingKey(Extension), '', Existing) then
  begin
    if Same(Existing, Clsid) then
      Result := False;
  end;
end;

function InitializeSetup(): Boolean;
var
  Destination, Existing: String;
begin
  Result := False;
  Destination := ExpandConstant('{localappdata}\Programs\SagePreview');
  if DirExists(Destination) then
  begin
    MsgBox('SagePreview is already present at:' + #13#10 + Destination + #13#10#13#10 +
      'This installer will not overwrite an existing SagePreview installation.' + #13#10 +
      'Uninstall the existing version using its own uninstaller before testing this new installer.',
      mbError, MB_OK);
    Exit;
  end;
  if RegQueryStringValue(HKCU, RunKey, 'SagePreview', Existing) and (Existing <> '') then
  begin
    MsgBox('A SagePreview login startup entry already exists. Stop and remove the old installation first.', mbError, MB_OK);
    Exit;
  end;
  if not CheckClassConflict('{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}')
    or not CheckClassConflict('{FA18D610-3865-450A-90A4-5CC8346E6C6A}')
    or not CheckClassConflict('{3B3B5629-77BF-4C18-8D45-348AED61E2C4}')
    or not CheckClassConflict('{A012A89F-0BE7-46EF-A811-73EB24684431}')
    or not CheckBindingConflict('.clip', '{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}')
    or not CheckBindingConflict('.kra', '{FA18D610-3865-450A-90A4-5CC8346E6C6A}')
    or not CheckBindingConflict('.psd', '{3B3B5629-77BF-4C18-8D45-348AED61E2C4}') then
  begin
    MsgBox('An older or incomplete SagePreview thumbnail COM registration exists.' + #13#10 +
      'Remove it using that version''s uninstaller. No changes were made.', mbError, MB_OK);
    Exit;
  end;
  Result := True;
end;

procedure RemoveOwnRegistration(const Extension, Clsid, DllName: String);
var
  ServerKey, ExtensionKey, Existing: String;
begin
  if not OwnServer(Clsid, DllName) then Exit;
  ExtensionKey := BindingKey(Extension);
  ServerKey := ClassBase(Clsid) + '\InprocServer32';
  if RegQueryStringValue(HKCU, ExtensionKey, '', Existing) and Same(Existing, Clsid) then
  begin
    RegDeleteValue(HKCU, ExtensionKey, '');
    RegDeleteKeyIfEmpty(HKCU, ExtensionKey);
  end;
  RegDeleteValue(HKCU, ServerKey, 'ThreadingModel');
  RegDeleteValue(HKCU, ServerKey, '');
  RegDeleteKeyIfEmpty(HKCU, ServerKey);
  RegDeleteKeyIfEmpty(HKCU, ClassBase(Clsid));
end;

procedure RegisterIfUnclaimed(const Extension, Clsid, DllName: String);
var
  Existing, ServerKey, ExtensionKey: String;
begin
  {Never override an effective foreign direct extension handler.}
  if RegQueryStringValue(HKCR, Extension + '\shellex\' + ThumbCategory, '', Existing)
    and (Existing <> '') then
  begin
    Log('Keeping existing ' + Extension + ' thumbnail handler ' + Existing);
    Exit;
  end;
  ExtensionKey := BindingKey(Extension);
  ServerKey := ClassBase(Clsid) + '\InprocServer32';
  if (not RegWriteStringValue(HKCU, ServerKey, '', ExpandConstant('{app}\' + DllName)))
    or (not RegWriteStringValue(HKCU, ServerKey, 'ThreadingModel', 'Apartment'))
    or (not RegWriteStringValue(HKCU, ExtensionKey, '', Clsid)) then
  begin
    RemoveOwnRegistration(Extension, Clsid, DllName);
    MsgBox('Could not register ' + Extension + ' thumbnails. Other formats can still work.' + #13#10 +
      'See the installation log and verify the registration before distributing this build.', mbError, MB_OK);
  end
  else
    Log('Registered ' + Extension + ' thumbnails.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then Exit;
  RegisterIfUnclaimed('.clip', '{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}', 'SageThumbnailProvider.dll');
  RegisterIfUnclaimed('.kra', '{FA18D610-3865-450A-90A4-5CC8346E6C6A}', 'SageKraThumbnailProvider.dll');
  RegisterIfUnclaimed('.psd', '{3B3B5629-77BF-4C18-8D45-348AED61E2C4}', 'SagePsdThumbnailProvider.dll');
  {AVIF: Windows generally owns thumbnails; do not register a competing handler.}
  if WizardIsTaskSelected('startup') then
  begin
    if not RegValueExists(HKCU, RunKey, 'SagePreview') then
      RegWriteStringValue(HKCU, RunKey, 'SagePreview', '"' + ExpandConstant('{app}\SagePreviewListener.exe') + '"');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
  OldEntry, ExpectedEntry: String;
begin
  if CurUninstallStep <> usUninstall then Exit;
  {Stop this listener normally before Inno removes the executables.}
  if FileExists(ExpandConstant('{app}\SagePreviewListener.exe')) then
    Exec(ExpandConstant('{app}\SagePreviewListener.exe'), '--stop', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  RemoveOwnRegistration('.clip', '{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}', 'SageThumbnailProvider.dll');
  RemoveOwnRegistration('.kra', '{FA18D610-3865-450A-90A4-5CC8346E6C6A}', 'SageKraThumbnailProvider.dll');
  RemoveOwnRegistration('.psd', '{3B3B5629-77BF-4C18-8D45-348AED61E2C4}', 'SagePsdThumbnailProvider.dll');
  RemoveOwnRegistration('.avif', '{A012A89F-0BE7-46EF-A811-73EB24684431}', 'SageAvifThumbnailProvider.dll');
  if RegQueryStringValue(HKCU, RunKey, 'SagePreview', OldEntry) then
  begin
    ExpectedEntry := '"' + ExpandConstant('{app}\SagePreviewListener.exe') + '"';
    if Same(OldEntry, ExpectedEntry) then
      RegDeleteValue(HKCU, RunKey, 'SagePreview');
  end;
end;
