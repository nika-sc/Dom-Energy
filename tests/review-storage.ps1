$ErrorActionPreference='Stop'
$text=Get-Content firmware/cydv4/archive_transaction.h -Raw
$body=$text.Substring($text.IndexOf('{'))
$time=Get-Content firmware/cydv4/runtime_time.h -Raw
$deadline=[regex]::Match($time,'beforeDeadline\([^)]*\)(\{[^}]+\})').Groups[1].Value.Replace('until &&','until!=0 &&').Replace('(int32_t)','(int)')
$code=@"
using System;
using System.Collections.Generic;
public class SimulatedPowerCut:Exception{}
public class MockArchiveDisk {
 public Dictionary<string,string> files=new Dictionary<string,string>();public int failAt,crashAt,calls;
 public bool exists(string name){return files.ContainsKey(name);}
 public bool rename(string a,string b){calls++;if(calls==failAt||!exists(a)||exists(b))return false;files[b]=files[a];files.Remove(a);if(calls==crashAt)throw new SimulatedPowerCut();return true;}
}
public static class StorageRegression {
 static uint now;
 static uint millis(){return now;}
 static bool Before(uint until) $deadline
 static bool Swap(MockArchiveDisk disk,string live,string archive,string revision,string staged) $body
 static void Assert(bool value,string name){if(!value)throw new Exception(name);}
 static MockArchiveDisk Disk(){var d=new MockArchiveDisk();d.files["live"]="NEW DATA";d.files["archive"]="OLD DATA";d.files["staged"]="HEADER";return d;}
 static void Preserved(MockArchiveDisk disk){Assert(disk.files.ContainsValue("NEW DATA")&&disk.files.ContainsValue("OLD DATA"),"measurement data lost");}
 public static string Run(){
  for(int fail=0;fail<=3;fail++){
   var d=Disk();d.failAt=fail;bool ok=Swap(d,"live","archive","revision","staged");Preserved(d);
   if(fail==0)Assert(ok&&d.files["live"]=="HEADER"&&d.files["archive"]=="NEW DATA","normal commit");
   if(!d.exists("live")){d.failAt=0;Assert(Swap(d,"live","archive","revision","staged"),"resume staged working file");Preserved(d);}
  }
  for(int crash=1;crash<=3;crash++){
   var d=Disk();d.crashAt=crash;
   try{Swap(d,"live","archive","revision","staged");}catch(SimulatedPowerCut){}
   Preserved(d);d.crashAt=0;
   if(!d.exists("live")||d.files["live"]!="HEADER")Assert(Swap(d,"live","archive","revision","staged"),"power-cut recovery");
   Preserved(d);Assert(d.files["live"]=="HEADER"&&d.files["archive"]=="NEW DATA","recovered commit");
  }
  var collision=Disk();collision.files["revision"]="EARLIER REVISION";
  Assert(!Swap(collision,"live","archive","revision","staged"),"existing revision overwritten");Preserved(collision);
  var first=Disk();first.files.Remove("archive");Assert(Swap(first,"live","archive","revision","staged"),"first month archive");
  now=uint.MaxValue-50;Assert(Before(100),"wrapped deadline fired early");now=99;Assert(Before(100),"deadline waiting");now=100;Assert(!Before(100),"deadline at equality");now=101;Assert(!Before(100),"elapsed deadline");Assert(!Before(0),"idle deadline");
  return "PASS: actual archive transaction body, every rename failure, simulated cuts after every rename, recovery, collisions and 49.7-day timer rollover";
 }
}
"@
Add-Type -TypeDefinition $code
[StorageRegression]::Run()
