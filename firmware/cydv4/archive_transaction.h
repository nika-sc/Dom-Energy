#pragma once
// No measurement file is deleted. A prior archive becomes an immutable revision.
// The caller has written and checked a header-only staged working file.
template<class Store>
bool archiveSwap(Store& disk,const char* live,const char* archive,const char* revision,const char* staged) {
  if (!disk.exists(live)) return disk.exists(archive) && disk.exists(staged) && disk.rename(staged,live);
  bool prior=false;
  if (disk.exists(archive)) {
    if (disk.exists(revision) || !disk.rename(archive,revision)) return false;
    prior=true;
  }
  if (!disk.rename(live,archive)) {
    if (prior) disk.rename(revision,archive);
    return false;
  }
  return disk.rename(staged,live);
}
