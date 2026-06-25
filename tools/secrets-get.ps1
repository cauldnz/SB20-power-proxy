#requires -Version 7
<#
.SYNOPSIS
  Read an Infisical machine-identity credential back out of the Windows Credential Manager
  (the counterpart to secrets-pull.ps1) so the build can authenticate non-interactively.

.DESCRIPTION
  Returns the stored {host, projectId, env, clientId, clientSecret} for an identity. By default the
  clientSecret is masked; pass -Field clientSecret (scripting) or -AsEnv (build) to emit it.

  Typical build use -- log in with these to mint a short-lived token, then pull the project's
  secrets (e.g. OTA_PASSWORD):

      $cid = ./secrets-get.ps1 -Field clientId
      $cs  = ./secrets-get.ps1 -Field clientSecret
      infisical login --method=universal-auth --client-id=$cid --client-secret=$cs --plain --silent

.EXAMPLE
  ./secrets-get.ps1                                  # masked summary object
.EXAMPLE
  ./secrets-get.ps1 -Field clientSecret              # raw secret (for scripting / the build)
.EXAMPLE
  ./secrets-get.ps1 -AsEnv | Set-Content creds.env   # KEY=VALUE block incl. the secret
#>
[CmdletBinding()]
param(
    [string]$Identity = 'sb20-power-proxy',
    [string]$Target,
    [ValidateSet('host', 'projectId', 'env', 'clientId', 'clientSecret')]
    [string]$Field,
    [switch]$AsEnv
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Target) { $Target = "SB20/infisical/$Identity" }

if (-not ('SB20.CredManRead' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace SB20 {
  public static class CredManRead {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct CREDENTIAL {
      public uint Flags; public uint Type; public string TargetName; public string Comment;
      public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
      public uint CredentialBlobSize; public IntPtr CredentialBlob; public uint Persist;
      public uint AttributeCount; public IntPtr Attributes; public string TargetAlias; public string UserName;
    }
    [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode, EntryPoint="CredReadW")]
    public static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", SetLastError=true, EntryPoint="CredFree")]
    public static extern void CredFree(IntPtr cred);
  }
}
'@
}

$out = [IntPtr]::Zero
if (-not [SB20.CredManRead]::CredRead($Target, 1, 0, [ref]$out)) {   # 1 = CRED_TYPE_GENERIC
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($err -eq 1168) {   # ERROR_NOT_FOUND
        throw "No stored credential '$Target'. Run secrets-pull.ps1 -Identity $Identity first."
    }
    throw "CredRead('$Target') failed (Win32 $err)"
}
try {
    $cred  = [System.Runtime.InteropServices.Marshal]::PtrToStructure($out, [type]'SB20.CredManRead+CREDENTIAL')
    $bytes = [byte[]]::new($cred.CredentialBlobSize)
    [System.Runtime.InteropServices.Marshal]::Copy($cred.CredentialBlob, $bytes, 0, $cred.CredentialBlobSize)
    $json  = [System.Text.Encoding]::Unicode.GetString($bytes)
}
finally {
    [SB20.CredManRead]::CredFree($out)
}
$data = $json | ConvertFrom-Json

if ($Field) { Write-Output $data.$Field; return }
if ($AsEnv) {
    "INFISICAL_HOST=$($data.host)"
    "INFISICAL_PROJECT_ID=$($data.projectId)"
    "INFISICAL_ENV=$($data.env)"
    "INFISICAL_CLIENT_ID=$($data.clientId)"
    "INFISICAL_CLIENT_SECRET=$($data.clientSecret)"
    return
}
[pscustomobject]@{
    host         = $data.host
    projectId    = $data.projectId
    env          = $data.env
    clientId     = $data.clientId
    clientSecret = '********'
}
