$root = "C:\Users\xuany\Documents\dualPad"
$src  = Join-Path $root "src\haptics"
$dst  = Join-Path $root "src\legacy\haptics"

New-Item -ItemType Directory -Force -Path $dst | Out-Null

$files = @(
  "EventCollector.cpp","EventCollector.h",
  "EventQueue.cpp","EventQueue.h",
  "EventShortWindowCache.cpp","EventShortWindowCache.h",
  "EventWindowScorer.cpp","EventWindowScorer.h",
  "FileAtomicWriter.cpp","FileAtomicWriter.h",
  "FormSemanticCache.cpp","FormSemanticCache.h",
  "HapticsMetrics.cpp","HapticsMetrics.h",
  "HapticTemplateCache.cpp","HapticTemplateCache.h",
  "MatchResultCache.cpp","MatchResultCache.h",
  "SemanticCacheIO.cpp","SemanticCacheIO.h",
  "SemanticCacheTypes.h",
  "SemanticFingerprint.cpp","SemanticFingerprint.h",
  "SemanticRules.cpp","SemanticRules.h",
  "SemanticWarmupService.cpp","SemanticWarmupService.h",
  "SoundFormScanner.cpp","SoundFormScanner.h",
  "SubmitFeatureCache.cpp","SubmitFeatureCache.h",
  "VoiceFormatRegistry.cpp","VoiceFormatRegistry.h"
)

foreach ($f in $files) {
  $from = Join-Path $src $f
  if (Test-Path $from) {
    Move-Item -Path $from -Destination $dst -Force
    Write-Host "[Moved] $f"
  } else {
    Write-Host "[Skip ] $f (not found)"
  }
}

Write-Host "Done. Moved files to: $dst"