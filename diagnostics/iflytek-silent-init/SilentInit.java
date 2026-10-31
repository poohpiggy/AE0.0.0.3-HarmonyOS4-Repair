import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.*;

/** Fresh-instance iFlytek 26-key/authorized-privacy candidate; never log prefs. */
public final class SilentInit {
 static final String[] PRIVACY={"privacy_authorized_again","privacy_mode_selected","privacy_policy_authorized_new","privacy_review_vesion","privacy_only_update_vesion_when_next_check","privacy_last_confirm_time"};
 static final String[] LAYOUT={"key_input_layout_type","language_chinese_layout","key_keyboard_select_wizard","new_input_mode_layout"};
 static String prepare(File file,String[] forbidden,String insertion) throws Exception {
  if(Files.isSymbolicLink(file.toPath()))throw new IllegalArgumentException();
  byte[] data=file.exists()?Files.readAllBytes(file.toPath()):"<map/>".getBytes(StandardCharsets.UTF_8);
  if(data.length==0||data.length>1048576)throw new IllegalArgumentException();
  String xml=new String(data,StandardCharsets.UTF_8);
  if(xml.toUpperCase(java.util.Locale.ROOT).contains("<!DOCTYPE"))throw new IllegalArgumentException();
  DocumentBuilderFactory f=DocumentBuilderFactory.newInstance();f.setExpandEntityReferences(false);
  javax.xml.parsers.DocumentBuilder b=f.newDocumentBuilder();b.setErrorHandler(new org.xml.sax.helpers.DefaultHandler());
  Document d=b.parse(new java.io.ByteArrayInputStream(data));
  if(!"map".equals(d.getDocumentElement().getTagName()))throw new IllegalArgumentException();
  NodeList all=d.getDocumentElement().getChildNodes();
  for(int i=0;i<all.getLength();i++)if(all.item(i) instanceof Element)for(String key:forbidden)
   if(key.equals(((Element)all.item(i)).getAttribute("name")))throw new IllegalArgumentException();
  int pos=xml.lastIndexOf("</map>");
  if(pos>=0 && xml.substring(pos+6).trim().isEmpty())return xml.substring(0,pos)+insertion+xml.substring(pos);
  java.util.regex.Matcher m=java.util.regex.Pattern.compile("<map\\s*/>\\s*$").matcher(xml);
  if(m.find())return xml.substring(0,m.start())+"<map>\n"+insertion+"</map>\n";
  throw new IllegalArgumentException();
 }
 static void write(File dest,String s) throws Exception {
  if(!dest.isFile()||Files.isSymbolicLink(dest.toPath()))throw new IllegalArgumentException();
  try(FileOutputStream out=new FileOutputStream(dest,false)){out.write(s.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
 }
 public static void main(String[] a) {
  try {
   if(a.length!=6)throw new IllegalArgumentException();
   String privacy="<boolean name=\"privacy_authorized_again\" value=\"true\" />\n"+
    "<boolean name=\"privacy_mode_selected\" value=\"true\" />\n"+
    "<boolean name=\"privacy_policy_authorized_new\" value=\"true\" />\n"+
    "<string name=\"privacy_review_vesion\">1.01</string>\n"+
    "<boolean name=\"privacy_only_update_vesion_when_next_check\" value=\"false\" />\n"+
    "<long name=\"privacy_last_confirm_time\" value=\""+System.currentTimeMillis()+"\" />\n";
   String layout="<int name=\"key_input_layout_type\" value=\"1\" />\n"+
    "<int name=\"language_chinese_layout\" value=\"1\" />\n"+
    "<int name=\"key_keyboard_select_wizard\" value=\"0\" />\n"+
    "<string name=\"new_input_mode_layout\">0,1</string>\n";
   String p=prepare(new File(a[0]),PRIVACY,privacy),l=prepare(new File(a[1]),LAYOUT,layout);
   String skin=prepare(new File(a[2]),new String[]{"current_layout_id"},"<int name=\"current_layout_id\" value=\"6\" />\n");
   write(new File(a[3]),p);write(new File(a[4]),l);write(new File(a[5]),skin);
  }catch(Exception e){System.err.println("iFlytek fresh-init preflight refused or failed");System.exit(1);}
 }
}
