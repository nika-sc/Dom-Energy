$ErrorActionPreference='Stop'
$s=Get-Content firmware/cydv4/cydv4.ino -Raw -Encoding UTF8
function Body($signature){$start=$s.IndexOf('{',$s.IndexOf($signature));$level=1;$end=$start+1;while($level){if($s[$end] -eq '{'){$level++};if($s[$end] -eq '}'){$level--};$end++};$s.Substring($start,$end-$start)}
$add=Body 'static int dayAdd('
$offset=Body 'static size_t findDayStartOffset('
$offset=$offset.Replace('size_t','uint').Replace('uint8_t','byte').Replace('char pat[16];','byte* pat=stackalloc byte[16];').Replace('sizeof(pat)','16').Replace('char buf[CH + 32 + 1];','byte* buf=stackalloc byte[(int)CH+33];').Replace('char first[11];','byte* first=stackalloc byte[11];')
$code=@"
using System;using System.Text;using System.Collections.Generic;
public unsafe class MemoryCsv {
 byte[] data;uint pos;public MemoryCsv(byte[] x){data=x;}
 public uint size(){return (uint)data.Length;}public bool seek(uint x){pos=x;return x<=data.Length;}
 public uint read(byte* target,uint count){uint got=Math.Min(count,size()-pos);for(uint i=0;i<got;i++)target[i]=data[pos+i];pos+=got;return got;}
}
public static unsafe class HistoryIndexRegression {
 static byte[][] sDayList=new byte[46][];static int sDayN;static int[] sDayGapN=new int[46],sDayGapMi=new int[46];
 static int memcmp(byte[] a,byte[] b,int count){for(int i=0;i<count;i++){if(a[i]!=b[i])return a[i]-b[i];}return 0;}
 static int strcmp(byte[] a,byte[] b){return memcmp(a,b,10);}
 static void memcpy(byte[] a,byte[] b,int count){Array.Copy(b,a,count);}
 static int memcmp(byte* a,byte* b,int count){for(int i=0;i<count;i++)if(a[i]!=b[i])return a[i]-b[i];return 0;}
 static uint strlen(byte* a){uint n=0;while(a[n]!=0)n++;return n;}
 static void snprintf(byte* target,int size,string format,byte* day){target[0]=10;int i=0;for(;i<10;i++)target[i+1]=day[i];target[i+1]=0;}
 static int Add(byte[] d) $add
 static uint Offset(MemoryCsv f,byte* dayPrefix) $offset
 static void Assert(bool b,string m){if(!b)throw new Exception(m);}
 public static string Run(){
  for(int i=0;i<46;i++)sDayList[i]=new byte[11];
  DateTime first=new DateTime(2024,1,1);
  for(int i=0;i<100;i++)Add(Encoding.ASCII.GetBytes(first.AddDays(i).ToString("yyyy-MM-dd")));
  var got=new List<string>();for(int i=0;i<sDayN;i++)got.Add(Encoding.ASCII.GetString(sDayList[i],0,10));
  Assert(sDayN==46,"date count");for(int i=54;i<100;i++)Assert(got.Contains(first.AddDays(i).ToString("yyyy-MM-dd")),"date array shifted/corrupted");
  Assert(Add(Encoding.ASCII.GetBytes("2023-01-01"))==-1,"old day displaced newer day");
  byte[] day=Encoding.ASCII.GetBytes("2024-01-01\0");
  fixed(byte* p=day){
   for(int lines=1;lines<=300;lines++){
    string prefix="datetime,voltage\n2023-12-31 23:59:30,230,1,1,1,1,20\n";
    var text=new StringBuilder(prefix);for(int i=0;i<lines;i++)text.Append("2024-01-01 00:00:00,230,1,1,1,1,20\n");
    uint found=Offset(new MemoryCsv(Encoding.ASCII.GetBytes(text.ToString())),p);
    Assert(found==(uint)prefix.Length,"day boundary lost: "+lines);
   }
   Assert(Offset(new MemoryCsv(Encoding.ASCII.GetBytes("2024-01-01,00:00,20,50,1013\n2024-01-01,01:00,21,50,1013\n")),p)==0,"headerless first row lost");
  }
  return "PASS: source-extracted latest 46 dates across 100 days and 300 block-boundary cases, including headerless climate logs";
 }
}
"@
Add-Type -TypeDefinition $code -CompilerParameters (New-Object CodeDom.Compiler.CompilerParameters -Property @{CompilerOptions='/unsafe'})
[HistoryIndexRegression]::Run()
