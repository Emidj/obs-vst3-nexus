using System;
using System.IO;
using System.IO.Compression;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Windows.Forms;
using System.Drawing.Drawing2D;

[assembly: AssemblyTitle("OBS VST3 NEXUS Setup")]
[assembly: AssemblyProduct("OBS VST3 NEXUS")]
[assembly: AssemblyCompany("DjEmiMas")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]
[assembly: AssemblyInformationalVersion("1.0")]
sealed class NexusForm:Form {
 public bool PreviewOnly;
 protected override bool ShowWithoutActivation {get{return PreviewOnly;}}
}
sealed class NexusButton:Button {
 public bool Primary;
 bool hovering;
 public NexusButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);Cursor=Cursors.Hand;FlatStyle=FlatStyle.Flat;}
 protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
 protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
 protected override void OnPaint(PaintEventArgs e){
  var color=Primary?(hovering?Color.FromArgb(224,39,52):Color.FromArgb(190,24,38)):(hovering?Color.FromArgb(55,63,80):Color.FromArgb(37,43,56));
  e.Graphics.Clear(color);using(var pen=new Pen(Primary?Color.FromArgb(235,72,83):Color.FromArgb(72,82,103)))e.Graphics.DrawRectangle(pen,0,0,Width-1,Height-1);
  TextRenderer.DrawText(e.Graphics,Text,Font,ClientRectangle,Enabled?Color.White:Color.FromArgb(145,150,165),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix);
  if(Focused)ControlPaint.DrawFocusRectangle(e.Graphics,new Rectangle(5,5,Width-10,Height-10),Color.White,color);
 }
}
static class Setup {
 static string L(string key){return NexusLanguages.Text(key);}
 const string Product="OBS VST3 NEXUS";
 static bool fixtureTests;
 static string DefaultRoot { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"obs-studio","plugins","obs-vst3-nexus"); } }
 static string Hash(byte[] b){using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(b)).Replace("-","").ToLowerInvariant();}
 static Dictionary<string,byte[]> Payload(){
  var result=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
  using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))using(var zip=new ZipArchive(s,ZipArchiveMode.Read)){
   foreach(var e in zip.Entries){if(e.FullName.EndsWith("/"))continue;var name=e.FullName.Replace('/',Path.DirectorySeparatorChar);
    if(Path.IsPathRooted(name)||name.Split(Path.DirectorySeparatorChar).Any(x=>x==".."))throw new IOException(L("Ruta no válida en el paquete."));
    using(var input=e.Open())using(var output=new MemoryStream()){input.CopyTo(output);result.Add(name,output.ToArray());}
   }
  }
  if(!result.ContainsKey(@"bin\64bit\obs-vst3-nexus.dll")||!result.ContainsKey(@"bin\64bit\nexus_vst3_worker.exe"))throw new IOException(L("Paquete incompleto."));
  return result;
 }
 static string Full(string p){return Path.GetFullPath(p).TrimEnd(Path.DirectorySeparatorChar);}
 static void Closed(){if(fixtureTests)return;foreach(var name in new[]{"obs64","obs32","obs"}){var ps=Process.GetProcessesByName(name);bool found=ps.Length>0;foreach(var p in ps)p.Dispose();if(found)throw new IOException(L("Cierra OBS antes de continuar."));}}
 static void Atomic(string path,byte[] b){Directory.CreateDirectory(Path.GetDirectoryName(path));var tmp=path+"."+Guid.NewGuid().ToString("N")+".tmp";try{File.WriteAllBytes(tmp,b);if(File.Exists(path))File.Replace(tmp,path,null);else File.Move(tmp,path);}finally{if(File.Exists(tmp))File.Delete(tmp);}}
 static Dictionary<string,string> Record(string root){
  var r=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);var p=Path.Combine(root,"nexus-install.txt");if(!File.Exists(p))return r;
  foreach(var line in File.ReadAllLines(p)){int split=line.IndexOf("  ",StringComparison.Ordinal);if(split!=64)throw new IOException(L("Registro de instalación no válido."));var name=line.Substring(66);if(!Payload().ContainsKey(name))throw new IOException(L("Registro con archivos desconocidos."));r.Add(name,line.Substring(0,64));}return r;
 }
 static void Apply(string root,bool uninstall,int failAfter=-1,bool simulateRunning=false){
  if(simulateRunning)throw new IOException("OBS en ejecución (prueba).");Closed();root=Full(root);
  if(root.Length>180)throw new IOException(L("Destination path is too long. Choose a shorter folder."));
  if(!Path.GetFileName(root).Equals("obs-vst3-nexus",StringComparison.OrdinalIgnoreCase))throw new IOException(L("La carpeta de destino debe llamarse obs-vst3-nexus."));
  var payload=Payload();var record=Record(root);var changes=new Dictionary<string,byte[]>();
  foreach(var f in payload){var path=Path.Combine(root,f.Key);byte[] before=File.Exists(path)?File.ReadAllBytes(path):null;
   string prior;bool owned=record.TryGetValue(f.Key,out prior);
   if(before!=null&&(!owned||Hash(before)!=prior))throw new IOException(L("Se conserva un archivo ajeno o modificado: ")+path);
   if(!uninstall||owned)changes[path]=uninstall?null:f.Value;
  }
  var marker=Path.Combine(root,"nexus-install.txt");changes[marker]=uninstall?null:System.Text.Encoding.UTF8.GetBytes(String.Join("\n",payload.Select(f=>Hash(f.Value)+"  "+f.Key)));
  var previous=changes.ToDictionary(f=>f.Key,f=>File.Exists(f.Key)?File.ReadAllBytes(f.Key):null);
  var existing=previous.Where(f=>f.Value!=null).ToArray();
  if(existing.Length>0){var backup=Path.Combine(Path.GetDirectoryName(root),"obs-vst3-nexus-backups",DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff"));foreach(var f in existing)Atomic(Path.Combine(backup,f.Key.Substring(root.Length+1)),f.Value);}
  var done=new List<string>();try{int count=0;foreach(var f in changes){if(failAfter>=0&&count++==failAfter)throw new IOException("Fallo simulado.");done.Add(f.Key);if(f.Value==null){if(File.Exists(f.Key))File.Delete(f.Key);}else Atomic(f.Key,f.Value);}}
  catch{foreach(var path in done.AsEnumerable().Reverse()){if(previous[path]==null){if(File.Exists(path))File.Delete(path);}else Atomic(path,previous[path]);}throw;}
 }
 static void Tests(string folder){
  // Fixture paths are always nested beneath the supplied QA directory, never the installed plugin.
  fixtureTests=true;
  folder=Full(folder);Directory.CreateDirectory(folder);var root=Path.Combine(folder,"Prueba ñ Ω","obs-vst3-nexus");Directory.CreateDirectory(root);File.WriteAllText(Path.Combine(root,"usuario.txt"),"conservar");
  Apply(root,false);foreach(var f in Payload())if(Hash(File.ReadAllBytes(Path.Combine(root,f.Key)))!=Hash(f.Value))throw new Exception("Hash incorrecto.");
  Apply(root,false);if(!Directory.Exists(Path.Combine(Path.GetDirectoryName(root),"obs-vst3-nexus-backups")))throw new Exception("Sin copia de seguridad.");
  try{Apply(root,true,2);throw new Exception("No se simuló el fallo.");}catch(IOException){}
  foreach(var f in Payload())if(Hash(File.ReadAllBytes(Path.Combine(root,f.Key)))!=Hash(f.Value))throw new Exception("Rollback incorrecto.");
  try{Apply(root,false,-1,true);throw new Exception("No se rechazó OBS abierto.");}catch(IOException){}
  var dll=Path.Combine(root,@"bin\64bit\obs-vst3-nexus.dll");var bytes=File.ReadAllBytes(dll);File.WriteAllText(dll,"modificado");
  try{Apply(root,true);throw new Exception("No se conservó el archivo modificado.");}catch(IOException){}if(File.ReadAllText(dll)!="modificado")throw new Exception("Archivo alterado.");File.WriteAllBytes(dll,bytes);
  Apply(root,true);Apply(root,true);if(!File.Exists(Path.Combine(root,"usuario.txt"))||File.Exists(dll))throw new Exception("Desinstalación incorrecta.");
  try{Apply(Path.Combine(folder,"destino-incorrecto"),false);throw new Exception("Destino inválido aceptado.");}catch(IOException){}
  var invalid=Path.Combine(folder,"registro","obs-vst3-nexus");Directory.CreateDirectory(invalid);File.WriteAllText(Path.Combine(invalid,"nexus-install.txt"),"incorrecto");try{Apply(invalid,false);throw new Exception("Registro inválido aceptado.");}catch(IOException){}
  try{Apply(Path.Combine(folder,new string('x',190),"obs-vst3-nexus"),false);throw new Exception("Long destination accepted");}catch(IOException){}
  var foreign=Path.Combine(folder,"foreign","obs-vst3-nexus");Directory.CreateDirectory(Path.Combine(foreign,"bin","64bit"));File.WriteAllText(Path.Combine(foreign,@"bin\64bit\obs-vst3-nexus.dll"),"unowned");
  try{Apply(foreign,false);throw new Exception("Unowned file overwritten");}catch(IOException){}if(File.ReadAllText(Path.Combine(foreign,@"bin\64bit\obs-vst3-nexus.dll"))!="unowned")throw new Exception("Unowned file damaged");
  File.WriteAllText(Path.Combine(folder,"installer-tests.txt"),"PASS: payload hashes, Unicode destination, reinstall, backups, rollback, running-OBS refusal branch, modified/unowned file preservation, uninstall, repeated uninstall, invalid destination/record, long-path refusal, and unowned-file refusal. No normal OBS, registry or configuration modified.");
 }
 static void UpgradeTest(string folder,string oldZip){
  fixtureTests=true;folder=Full(folder);Directory.CreateDirectory(folder);
  string root=Path.Combine(folder,"upgrade","obs-vst3-nexus");Directory.CreateDirectory(root);
  var old=new Dictionary<string,byte[]>();var current=Payload();
  using(var input=File.OpenRead(oldZip))using(var zip=new ZipArchive(input,ZipArchiveMode.Read))foreach(var entry in zip.Entries){
   if(entry.FullName.EndsWith("/"))continue;string name=entry.FullName.Replace('/',Path.DirectorySeparatorChar);
   if(!current.ContainsKey(name))throw new IOException("Unexpected upgrade fixture entry");
   using(var stream=entry.Open())using(var data=new MemoryStream()){stream.CopyTo(data);old.Add(name,data.ToArray());}
  }
  foreach(var entry in old)Atomic(Path.Combine(root,entry.Key),entry.Value);
  Atomic(Path.Combine(root,"nexus-install.txt"),System.Text.Encoding.UTF8.GetBytes(String.Join("\n",old.Select(f=>Hash(f.Value)+"  "+f.Key))));
  File.WriteAllText(Path.Combine(root,"keep-user.txt"),"keep");Apply(root,false);
  foreach(var entry in current)if(Hash(File.ReadAllBytes(Path.Combine(root,entry.Key)))!=Hash(entry.Value))throw new Exception("Upgrade hash mismatch");
  string backup=Path.Combine(Path.GetDirectoryName(root),"obs-vst3-nexus-backups");
  if(!Directory.GetFiles(backup,"obs-vst3-nexus.dll",SearchOption.AllDirectories).Any(file=>Hash(File.ReadAllBytes(file))==Hash(old[@"bin\64bit\obs-vst3-nexus.dll"])))throw new Exception("Upgrade backup missing");
  Apply(root,true);if(!File.Exists(Path.Combine(root,"keep-user.txt")))throw new Exception("Upgrade uninstall removed unrelated file");
  File.WriteAllText(Path.Combine(folder,"upgrade-tests.txt"),"PASS: prior test-version upgrade, new payload hashes, retained old DLL backup, uninstall and unrelated-file preservation. Isolated fixture only.");
 }
 static bool Admin(){using(var id=WindowsIdentity.GetCurrent())return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);}
 static void Action(string root,bool uninstall){
  root=Full(root);Closed();
  if(!Admin()&&root.StartsWith(Full(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)){
   var info=new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,(uninstall?"/uninstall ":"/install ")+"\""+root+"\" /lang:"+NexusLanguages.Code){UseShellExecute=true,Verb="runas"};using(var p=Process.Start(info)){p.WaitForExit();if(p.ExitCode!=0)throw new IOException(L("No se completó la operación. Revisa el mensaje de la ventana de administrador."));}return;
  }
  Apply(root,uninstall);
 }
 [STAThread] static int Main(string[] args){try{
  foreach(var arg in args)if(arg.StartsWith("/lang:")){NexusLanguages.Selected=Array.IndexOf(NexusLanguages.Codes,arg.Substring(6));}
  args=args.Where(a=>!a.StartsWith("/lang:")).ToArray();
  if(args.Length==2&&args[0]=="/localecheck"){
   if(NexusLanguages.Rows.Any(r=>r.Length!=5||r.Any(String.IsNullOrEmpty)))throw new Exception("Incomplete translation table");
   File.WriteAllText(args[1],String.Join("\n",NexusLanguages.Codes.Select((code,index)=>{NexusLanguages.Selected=index;return code+": "+L("Install")+" / "+L("Uninstall")+" / "+L("Close");})));return 0;
  }
  if(args.Length==2&&args[0]=="/guardcheck"){
   try{Closed();File.WriteAllText(args[1],"OBS not running");}catch(IOException){File.WriteAllText(args[1],"PASS: running OBS refused before installation");}return 0;
  }
  if(args.Length==3&&args[0]=="/upgradecheck"){UpgradeTest(args[1],args[2]);return 0;}
  if(args.Length==2&&args[0]=="/selftest"){Tests(args[1]);return 0;}
  if(args.Length==2&&(args[0]=="/install"||args[0]=="/uninstall")){Action(args[1],args[0]=="/uninstall");return 0;}
  Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
  bool preview=args.Length==2&&args[0]=="/preview";
  var form=new NexusForm{PreviewOnly=preview,Text=Product+" 1.0",ClientSize=new Size(960,570),StartPosition=FormStartPosition.CenterScreen,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,BackColor=Color.FromArgb(25,29,38),ForeColor=Color.White,Font=new Font("Segoe UI",10),AutoScaleMode=AutoScaleMode.Dpi};
  var art=new PictureBox{Left=0,Top=0,Width=380,Height=570,BackColor=Color.Black,SizeMode=PictureBoxSizeMode.Zoom,TabStop=false};
  using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("nexus-sidebar.png"))using(var original=Image.FromStream(stream))art.Image=new Bitmap(original);
  using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("nexus-installer.ico"))using(var icon=new Icon(stream))form.Icon=(Icon)icon.Clone();
  var header=new Label{Text=Product+"  1.0",Left=416,Top=28,Width=310,Height=28,ForeColor=Color.FromArgb(165,174,193)};
  var title=new Label{Text=L("Install VST 3 NEXUS"),Left=416,Top=80,Width=510,Height=88,Font=new Font("Segoe UI",24,FontStyle.Bold)};
  var help=new Label{Text=L("One VST3 effect per filter · Windows x64 · OBS 31 / 32 / 33"),Left=416,Top=178,Width=510,Height=62,ForeColor=Color.FromArgb(165,174,193)};
  var folderLabel=new Label{Text=L("Installation folder"),Left=416,Top=260,Width=510,Height=26,ForeColor=Color.FromArgb(165,174,193)};
  var target=new TextBox{Text=DefaultRoot,Left=416,Top=294,Width=360,Height=32,BackColor=Color.FromArgb(33,38,49),ForeColor=Color.White,BorderStyle=BorderStyle.FixedSingle};
  var browse=new NexusButton{Text=L("Folder…"),Left=790,Top=289,Width=136,Height=38};
  browse.Click+=(s,e)=>{using(var dlg=new FolderBrowserDialog{Description=L("Select the parent folder for obs-vst3-nexus"),SelectedPath=Path.GetDirectoryName(target.Text)}){if(dlg.ShowDialog(form)==DialogResult.OK)target.Text=Path.Combine(dlg.SelectedPath,"obs-vst3-nexus");}};
  var notes=new Label{Text=L("Close OBS before installing or uninstalling.")+"\n\n"+L("Scenes and profiles are preserved. Commercial effects are not included."),Left=416,Top=352,Width=510,Height=96,ForeColor=Color.FromArgb(165,174,193)};
  var divider=new Panel{Left=416,Top=468,Width=510,Height=1,BackColor=Color.FromArgb(55,62,78)};
  var remove=new NexusButton{Text=L("Uninstall"),Left=416,Top=491,Width=155,Height=48};
  var close=new NexusButton{Text=L("Close"),Left=583,Top=491,Width=155,Height=48};
  var install=new NexusButton{Text=L("Install"),Left=750,Top=491,Width=176,Height=48,Primary=true};
  var credit=new LinkLabel{Text="a plugin from DjEmiMas",Left=416,Top=546,Width=510,Height=22,LinkColor=Color.FromArgb(190,199,218),ActiveLinkColor=Color.White,VisitedLinkColor=Color.FromArgb(190,199,218),LinkArea=new LinkArea(14,8)};
  credit.LinkClicked+=(s,e)=>Process.Start(new ProcessStartInfo("https://discord.gg/dhcqZPDYh"){UseShellExecute=true});
  form.Controls.AddRange(new Control[]{art,header,title,help,folderLabel,target,browse,notes,divider,remove,close,install,credit});
  foreach(Control c in form.Controls)if(c is Label||c is Button)c.Tag=c.Text;
  var languages=new ComboBox{Left=746,Top=24,Width=180,DropDownStyle=ComboBoxStyle.DropDownList,BackColor=Color.FromArgb(33,38,49),ForeColor=Color.White,FlatStyle=FlatStyle.Flat};
  languages.Items.AddRange(new object[]{"English","Español","Italiano","Français","Deutsch"});languages.SelectedIndex=NexusLanguages.Selected<0?0:NexusLanguages.Selected;form.Controls.Add(languages);
  languages.SelectedIndexChanged+=(s,e)=>{NexusLanguages.Selected=languages.SelectedIndex;foreach(Control c in form.Controls)if(c.Tag is string)c.Text=L((string)c.Tag);};
  Action<bool> run=uninstall=>{
   if(uninstall&&MessageBox.Show(form,L("Uninstall OBS VST3 NEXUS from this folder?"),Product,MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
   form.UseWaitCursor=true;install.Enabled=remove.Enabled=browse.Enabled=close.Enabled=languages.Enabled=false;
   try{Action(target.Text,uninstall);MessageBox.Show(form,L(uninstall?"Uninstalled. Unrelated files, settings and backups are preserved.":"Installed. Open OBS and add VST 3 NEXUS as an audio filter."),Product,MessageBoxButtons.OK,MessageBoxIcon.Information);}
   catch(Exception ex){MessageBox.Show(form,ex.Message,Product,MessageBoxButtons.OK,MessageBoxIcon.Error);}
   finally{form.UseWaitCursor=false;install.Enabled=remove.Enabled=browse.Enabled=close.Enabled=languages.Enabled=true;}
  };
  install.Click+=(s,e)=>run(false);remove.Click+=(s,e)=>run(true);close.Click+=(s,e)=>form.Close();form.AcceptButton=install;form.CancelButton=close;
  form.FormClosed+=(s,e)=>{art.Image.Dispose();form.Icon.Dispose();};
  if(preview){
   Directory.CreateDirectory(args[1]);form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-30000,-30000);form.Show();Application.DoEvents();
   for(int language=0;language<5;++language){
    languages.SelectedIndex=language;form.Refresh();Application.DoEvents();
    foreach(Control c in form.Controls)if(c is Label&&c.Text.Length>0){var measured=TextRenderer.MeasureText(c.Text,c.Font,new Size(c.Width,0),TextFormatFlags.WordBreak|TextFormatFlags.NoPrefix);if(measured.Height>c.Height)throw new Exception("Clipped label: "+NexusLanguages.Code+" "+c.Text);}
    using(var bitmap=new Bitmap(form.Width,form.Height)){form.DrawToBitmap(bitmap,new Rectangle(0,0,bitmap.Width,bitmap.Height));bitmap.Save(Path.Combine(args[1],NexusLanguages.Code+".png"));}
   }
   form.Close();form.Dispose();return 0;
  }
  Application.Run(form);return 0;
 }catch(Exception e){if(args.Length>0&&args[0]=="/selftest"){File.WriteAllText(Path.Combine(args[1],"installer-error.txt"),e.ToString());}else MessageBox.Show(e.Message,Product,MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}}
}
