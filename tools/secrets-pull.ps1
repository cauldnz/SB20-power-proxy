#requires -Version 7
<#
.SYNOPSIS
  Pull an Infisical machine-identity credential from the NAS (over SSH) into the Windows
  Credential Manager, so the SB20 build can read it back non-interactively.

.DESCRIPTION
  Two-pattern Infisical model (cauldnz-pos#1): a build/dev box authenticates with a least-privilege
  Universal-Auth machine identity (clientId/clientSecret -> short-lived token). The durable secret is
  the clientSecret; we keep it in Windows Credential Manager under target "SB20/infisical/<identity>",
  NEVER in Git. This is the Windows counterpart to the bash runbook at
  cauldnz-pos:infra/identities/README.md ("Windows: Credential Manager").

  Source of the creds block (a KEY=VALUE list of INFISICAL_* lines, exactly what
  infra/identities/new-machine-identity.sh prints / saves):
    - default        : a ".creds" file on the NAS, fetched via `ssh <host> "cat <path>"`.
    - -FromStdin     : a pasted block on stdin (e.g. the provisioner's one-time stdout for a
                       dev-box identity that was never written to a NAS file).

  Reads nothing back and prints no secret. Companion reader: secrets-get.ps1.

.EXAMPLE
  ./secrets-pull.ps1
      Pull sb20-power-proxy.creds from `unraid` into Credential Manager.

.EXAMPLE
  ./secrets-pull.ps1 -Identity chris-p1-sb20
      Pull a different identity's .creds file.

.EXAMPLE
  ssh unraid "sh /tmp/new-machine-identity.sh chris-p1 --project pos --write" | ./secrets-pull.ps1 -Identity chris-p1 -FromStdin
      Store a freshly-provisioned identity's one-time stdout (no NAS file needed).

.EXAMPLE
  ./secrets-pull.ps1 -SelfTest
      Parser self-check only -- no SSH, no Credential Manager write.
#>
[CmdletBinding()]
param(
    [string]$Identity = 'sb20-power-proxy',
    [string]$SshHost  = 'unraid',
    [string]$CredsPath,                       # default: /mnt/user/appdata/pos-infisical/identities/<Identity>.creds
    [string]$Target,                          # default: SB20/infisical/<Identity>
    [switch]$FromStdin,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Parse a provisioner KEY=VALUE block into an ordered map of its INFISICAL_* lines. Pure -- the
# unit-testable core (see -SelfTest); tolerant of blank lines, indentation and trailing whitespace.
function ConvertFrom-CredsBlock {
    param([Parameter(Mandatory)][string]$Text)
    $map = [ordered]@{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*(INFISICAL_\w+)\s*=\s*(.+?)\s*$') {
            $map[$Matches[1]] = $Matches[2]
        }
    }
    return $map
}

if ($SelfTest) {
    $sample = @"
  Machine identity provisioned: demo
  INFISICAL_HOST=http://wtrmax.local:8222
  INFISICAL_PROJECT_ID=proj_abc123
  INFISICAL_ENV=<one of: dev staging prod>
  INFISICAL_CLIENT_ID=cid_demo
  INFISICAL_CLIENT_SECRET=shhh-secret
"@
    $p = ConvertFrom-CredsBlock $sample
    $ok = ($p.Count -eq 5) -and
          ($p['INFISICAL_CLIENT_ID'] -eq 'cid_demo') -and
          ($p['INFISICAL_CLIENT_SECRET'] -eq 'shhh-secret') -and
          ($p['INFISICAL_HOST'] -eq 'http://wtrmax.local:8222')
    if ($ok) { Write-Host "SelfTest PASS (parsed $($p.Count) keys)"; exit 0 }
    Write-Error "SelfTest FAIL: $($p | ConvertTo-Json -Compress)"; exit 1
}

# --- Windows Credential Manager (advapi32) -------------------------------------------------------
if (-not ('SB20.CredMan' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace SB20 {
  public static class CredMan {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct CREDENTIAL {
      public uint Flags; public uint Type; public string TargetName; public string Comment;
      public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
      public uint CredentialBlobSize; public IntPtr CredentialBlob; public uint Persist;
      public uint AttributeCount; public IntPtr Attributes; public string TargetAlias; public string UserName;
    }
    [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode, EntryPoint="CredWriteW")]
    public static extern bool CredWrite(ref CREDENTIAL userCredential, uint flags);
  }
}
'@
}

function Set-StoredCred {
    param([string]$TargetName, [string]$User, [string]$Secret)
    $blob = [System.Text.Encoding]::Unicode.GetBytes($Secret)
    $ptr  = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($blob.Length)
    try {
        [System.Runtime.InteropServices.Marshal]::Copy($blob, 0, $ptr, $blob.Length)
        $cred = New-Object 'SB20.CredMan+CREDENTIAL'
        $cred.Type               = 1            # CRED_TYPE_GENERIC
        $cred.TargetName         = $TargetName
        $cred.UserName           = $User
        $cred.CredentialBlobSize = [uint32]$blob.Length
        $cred.CredentialBlob     = $ptr
        $cred.Persist            = 2            # CRED_PERSIST_LOCAL_MACHINE
        if (-not [SB20.CredMan]::CredWrite([ref]$cred, 0)) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "CredWrite('$TargetName') failed (Win32 $err)"
        }
    }
    finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($ptr)
    }
}

if (-not $CredsPath) { $CredsPath = "/mnt/user/appdata/pos-infisical/identities/$Identity.creds" }
if (-not $Target)    { $Target    = "SB20/infisical/$Identity" }

# 1. obtain the creds block
if ($FromStdin) {
    $block = [Console]::In.ReadToEnd()
    if (-not $block -or -not $block.Trim()) {
        throw "No stdin received. Pipe the provisioner output, or drop -FromStdin to fetch over SSH."
    }
}
else {
    Write-Host "Fetching '$Identity' creds from ${SshHost}:$CredsPath ..."
    $block = (& ssh $SshHost "cat $CredsPath" 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "ssh fetch failed (exit $LASTEXITCODE). SSH must be key-based / non-interactive. Output:`n$block"
    }
}

# 2. parse + validate
$creds = ConvertFrom-CredsBlock $block
foreach ($k in 'INFISICAL_CLIENT_ID', 'INFISICAL_CLIENT_SECRET', 'INFISICAL_PROJECT_ID', 'INFISICAL_HOST') {
    if (-not $creds[$k]) {
        throw "creds block is missing $k (parsed keys: $($creds.Keys -join ', '))"
    }
}

# 3. store the whole set as a compact JSON blob (UserName = clientId so it's legible in the UI)
$envVal = if ($creds['INFISICAL_ENV'] -and $creds['INFISICAL_ENV'] -notmatch '^<') { $creds['INFISICAL_ENV'] } else { '' }
$payload = [ordered]@{
    host         = $creds['INFISICAL_HOST']
    projectId    = $creds['INFISICAL_PROJECT_ID']
    env          = $envVal
    clientId     = $creds['INFISICAL_CLIENT_ID']
    clientSecret = $creds['INFISICAL_CLIENT_SECRET']
} | ConvertTo-Json -Compress
Set-StoredCred -TargetName $Target -User $creds['INFISICAL_CLIENT_ID'] -Secret $payload

# 4. masked summary -- never echo the secret
Write-Host "Stored -> Windows Credential Manager: $Target"
Write-Host "  host       $($creds['INFISICAL_HOST'])"
Write-Host "  projectId  $($creds['INFISICAL_PROJECT_ID'])"
if ($envVal) { Write-Host "  env        $envVal" }
Write-Host "  clientId   $($creds['INFISICAL_CLIENT_ID'])"
Write-Host "  secret     ********  (read back: ./secrets-get.ps1 -Identity $Identity -Field clientSecret)"
