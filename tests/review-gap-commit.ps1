$ErrorActionPreference='Stop'
$text=Get-Content -LiteralPath 'firmware/cydv4/cydv4.ino' -Raw -Encoding UTF8
$start=$text.IndexOf('static bool gfCommit(int* outIns)')
$end=$text.IndexOf('// Отмена: вернуть',$start)
$part=$text.Substring($start,$end-$start)
$body=$part.Substring($part.IndexOf('{')).Trim()
$body=$body.Replace('*outIns','outIns').Replace('size_t','int').Replace('uint64_t','ulong').Replace('ULL','UL').Replace('(unsigned long long)','(ulong)').Replace('(unsigned)','(uint)')
$body=$body.Replace('if(!target)','if(!target.Valid)').Replace('chk ? chk.size()','chk.Valid ? chk.size()')
$body=$body.Replace('gSdErrMsg[0] = 0',"gSdErrMsg[0] = '\0'")
foreach($name in @('ins','tmpSz','lines')){$body=$body.Replace('&'+$name,'ref '+$name)}
$code=@"
#pragma warning disable 0414, 0649
using System;
using System.Collections.Generic;
using File=DiskFile;
public class DiskFile { public bool Valid;public int length;public int size(){return length;}public void close(){} }
public class PrintSink {public void println(string x){}public void printf(string x,params object[] args){} }
public class GapState { public int gaps=1; }
public class GapEntry { public int rowN=2; }
public class CommitDisk {
 public Dictionary<string,string> files=new Dictionary<string,string>();public string fail="";public bool activated;public bool finalMismatch;public int mutations;
 public bool exists(string p){return files.ContainsKey(p);}
 public bool remove(string p){if(fail=="cleanup"&&p=="/energy.tmp")return false;if(fail=="backup_cleanup"&&p=="/energy.bak")return false;mutations++;return files.Remove(p);}
 public bool rename(string a,string b){if(fail=="backup_rename"&&a=="/energy.csv")return false;if(fail=="activate"&&a=="/energy.tmp")return false;if(!exists(a)||exists(b))return false;mutations++;files[b]=files[a];files.Remove(a);if(a=="/energy.tmp")activated=true;return true;}
 public DiskFile open(string p,int mode){return new DiskFile{Valid=exists(p),length=exists(p)?files[p].Length-(finalMismatch&&activated&&p=="/energy.csv"?1:0):0};}
 public ulong totalBytes(){return 1000000UL;}public ulong usedBytes(){return 0UL;}
}
public static class GapCommitRegression {
 static CommitDisk SD;static PrintSink Serial=new PrintSink();static GapState sGF=new GapState();static GapEntry[] sGf={new GapEntry()};
 static string gGapPath="/energy.csv",sGapCommitStage,sGapCommitCode;static bool gSdOk=true,statsSeeded;
 static int FILE_READ,gSdErrCode,sDaysFileSize,sDaysAt,gSdGen,sEvFileSize,sEvAt,gDayLoaded,gDayOffset,gLastDraw,gLastWinFull,gLastSig;
 static char[] gSdErrMsg=new char[100];static bool rewriteOk=true;static int inserted=2;
 static bool gapCommitFailure(string c){sGapCommitCode=c;return false;}
 static bool gfRewrite(string src,int expected,ref int ins,ref int size,ref int lines){if(!rewriteOk)return false;SD.files["/energy.tmp"]=new string('N',120);ins=inserted;size=120;lines=3;return true;}
 static void histCacheDrop(){}static void resetStats(){}static void seedStatsFromSd(){}static void dayLoadFromSd(int x){}
 static bool Commit(ref int outIns) $body
 static void Assert(bool ok,string name){if(!ok)throw new Exception(name);}
 static void Reset(){SD=new CommitDisk();SD.files["/energy.csv"]=new string('O',100);SD.files["/energy.bak"]=new string('B',90);sGF.gaps=1;sGf[0].rowN=2;gSdOk=true;rewriteOk=true;inserted=2;gSdGen=0;}
 static void Case(string fail,string expected){Reset();SD.fail=fail;if(fail=="cleanup")SD.files["/energy.tmp"]="OLD";int n=0;Assert(!Commit(ref n),"failure accepted "+fail);Assert(sGapCommitCode==expected,"wrong outcome "+fail+": "+sGapCommitCode);Assert(SD.files["/energy.csv"]==new string('O',100),"original changed "+fail);}
 public static string Run(){
  Reset();int n=0;sGf[0].rowN=0;Assert(!Commit(ref n)&&sGapCommitCode=="no_cloud_data"&&SD.mutations==0,"zero cloud records changed SD");
  Case("cleanup","cleanup_tmp_failed");Case("backup_cleanup","backup_cleanup_failed");Case("backup_rename","backup_rename_failed");Case("activate","activate_failed");
  Reset();rewriteOk=false;Assert(!Commit(ref n)&&sGapCommitCode=="rewrite_failed"&&SD.files["/energy.csv"]==new string('O',100),"rewrite rollback");
  Reset();inserted=1;Assert(!Commit(ref n)&&sGapCommitCode=="insert_count_mismatch"&&SD.files["/energy.csv"]==new string('O',100),"count rollback");
  Reset();SD.finalMismatch=true;Assert(!Commit(ref n)&&sGapCommitCode=="final_size_mismatch"&&gSdGen==1&&SD.files["/energy.bak"]==new string('O',100),"final verification accepted or lost backup");
  Reset();Assert(Commit(ref n)&&n==2&&sGapCommitCode=="success"&&SD.files["/energy.bak"]==new string('O',100),"successful commit");
  return "PASS: extracted gfCommit; no-data performs zero SD mutations; cleanup/rename/rewrite/count failures; final verification cannot report success; original backup retained";
 }
}
"@
Add-Type -TypeDefinition $code -WarningAction SilentlyContinue
[GapCommitRegression]::Run()
