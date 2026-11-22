# Execute source-extracted C-compatible guard logic without sending malformed frames to hardware.
$ErrorActionPreference='Stop'
$source=Get-Content firmware/cydv4/cydv4.ino -Raw -Encoding UTF8
function Body([string]$signature){
    $start=$source.IndexOf($signature);if($start -lt 0){throw 'Function missing'}
    $start=$source.IndexOf('{',$start);$level=1;$end=$start+1
    while($level -and $end -lt $source.Length){if($source[$end] -eq '{'){$level++};if($source[$end] -eq '}'){$level--};$end++}
    # The selected functions contain no braces in comments/string literals except character constants.
    return $source.Substring($start,$end-$start)
}
# Anchor the end of the JSON helper explicitly: it contains a literal '{'.
$frame=Body 'int parseFrame('
$jsonStart=$source.IndexOf('static const char* tuyaJsonPayload(')
$jsonStart=$source.IndexOf('{',$jsonStart)
$jsonEnd=$source.IndexOf("`n}",$jsonStart)+2
$json=$source.Substring($jsonStart,$jsonEnd-$jsonStart)
foreach($entry in @('frame','json')){
    $value=Get-Variable $entry -ValueOnly
    $value=$value.Replace('const uint8_t*','byte*').Replace('const char*','byte*').Replace('uint8_t','byte').Replace('uint32_t','uint').Replace('size_t','uint').Replace('nullptr','null').Replace('mbedtls_gcm_context gcm;','int gcm=0;')
    Set-Variable $entry $value
}
$code=@"
using System;
public static unsafe class ProtocolGuards {
 const int HEADER_LEN=18,IV_LEN=12,TAG_LEN=16,MBEDTLS_CIPHER_ID_AES=1;
 static int cryptoCalls;
 static void mbedtls_gcm_init(int* x){}
 static void mbedtls_gcm_setkey(int* x,int id,byte* k,int bits){}
 static void mbedtls_gcm_free(int* x){}
 static int mbedtls_gcm_auth_decrypt(int* x,uint count,byte* iv,int il,byte* ad,int al,byte* tag,int tl,byte* input,byte* output){cryptoCalls++;for(uint i=0;i<count;i++)output[i]=input[i];return 0;}
 static int Parse(byte* buf,uint len,byte* key,byte* cmdOut,byte* payloadOut,uint payloadMax) $frame
 static byte* Json(byte* payload,int length) $json
 static void Assert(bool condition,string message){if(!condition)throw new Exception(message);}
 public static string Run(){
  byte[] bytes=new byte[1024],output=new byte[258];byte cmd=0;
  fixed(byte* b=bytes,o=output){
   for(int count=0;count<=300;count++){
    Array.Clear(bytes,0,bytes.Length);for(int i=0;i<output.Length;i++)output[i]=0xA5;
    int length=count+IV_LEN+TAG_LEN,total=HEADER_LEN+length+4;
    b[2]=0x66;b[3]=0x99;b[16]=(byte)(length>>8);b[17]=(byte)length;b[total-2]=0x99;b[total-1]=0x66;
    cryptoCalls=0;int result=Parse(b,(uint)total,b,&cmd,o,256);
    Assert(result==(count<256?count:-1),"payload length "+count);
    Assert(cryptoCalls==(count<256?1:0),"invalid length reached crypto");
    Assert(o[256]==0xA5 && o[257]==0xA5,"canary overwritten");
    if(result>=0)o[result]=0;
    Assert(o[256]==0xA5,"terminator exceeded output");
    b[total-1]=0;Assert(Parse(b,(uint)total,b,&cmd,o,256)==-1,"bad footer");b[total-1]=0x66;
    Assert(Parse(b,(uint)(total-1),b,&cmd,o,256)==-1,"mismatched total length");
   }
   for(uint length=0;length<50;length++)Assert(Parse(b,length,b,&cmd,o,256)==-1,"short frame");
   byte[] payload=System.Text.Encoding.ASCII.GetBytes("\0\0\0\0"+"3.5"+new string('\0',12)+"{\"dps\":{\"6\":\"CQAAAAAAABI=\"}}");
   fixed(byte* p=payload){Assert(Json(p,payload.Length)==p+19,"versioned push JSON lost");Assert(Json(p+4,payload.Length-4)==p+19,"push without return code lost");Assert(Json(p+19,payload.Length-19)==p+19,"bare JSON lost");p[3]=1;Assert(Json(p,payload.Length)==null,"nonzero return code accepted");}
  }
  return "PASS: source-extracted guards, 301 payload lengths, terminator canaries, malformed lengths/footer and versioned Tuya push";
 }
}
"@
Add-Type -TypeDefinition $code -CompilerParameters (New-Object CodeDom.Compiler.CompilerParameters -Property @{CompilerOptions='/unsafe'})
[ProtocolGuards]::Run()
