# Installs Pantomimus for this user (%LOCALAPPDATA%\Programs\Pantomimus, Start menu and Desktop shortcuts named
# "Atelier - Pantomimus"), through install.bat.
$ErrorActionPreference = 'Stop'
& cmd /c "`"$PSScriptRoot\install.bat`" $args"
exit $LASTEXITCODE
