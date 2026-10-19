Add-Type -AssemblyName System.Drawing
$refs=@("$PSHOME\System.Drawing.Common.dll","$PSHOME\System.Private.Windows.GdiPlus.dll","$PSHOME\System.Private.Windows.Core.dll")+@(Get-ChildItem -LiteralPath (Join-Path $PSHOME 'ref') -Filter '*.dll' | Select-Object -ExpandProperty FullName)
Add-Type -ReferencedAssemblies $refs -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
public class NaboRound2 {
 static double Smooth(double v){v=Math.Max(0,Math.Min(1,v));return v*v*(3-2*v);}
 static double Protect(int x,int y,int l,int t,int r,int b){return Smooth(Math.Min(Math.Min(x-l,r-x),Math.Min(y-t,b-y))/4.0);}
 static double Weight(int x,int y){
  double top=Smooth((380-y)/8.0),left=Smooth((300-x)/8.0)*Smooth((635-y)/8.0),right=Smooth((x-725)/8.0)*Smooth((635-y)/8.0);
  double w=Math.Max(top,Math.Max(left,right));
  w*=1-Protect(x,y,405,125,625,265);
  w*=1-Protect(x,y,300,290,725,640);
  w*=1-Protect(x,y,710,275,770,350);
  return w;
 }
 static void Save(Bitmap b,string p){if(File.Exists(p))throw new Exception("Refusing overwrite "+p);b.Save(p,ImageFormat.Png);}
 static Graphics HQ(Bitmap b){var g=Graphics.FromImage(b);g.CompositingMode=CompositingMode.SourceOver;g.CompositingQuality=CompositingQuality.HighQuality;g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.PixelOffsetMode=PixelOffsetMode.HighQuality;return g;}
 static Color Blend(Color old,Color patch,double w){
  double a=old.A*(1-w)+patch.A*w;if(a<0.5)return Color.Transparent;
  int r=(int)Math.Round((old.R*old.A*(1-w)+patch.R*patch.A*w)/a),g=(int)Math.Round((old.G*old.A*(1-w)+patch.G*patch.A*w)/a),b=(int)Math.Round((old.B*old.A*(1-w)+patch.B*patch.A*w)/a);
  if(g>r+4&&g>b+4)g=Math.Max(r,b);
  return Color.FromArgb((int)Math.Round(a),r,g,b);
 }
 public static string Run(string dir){
  using(var raw=new Bitmap(Path.Combine(dir,"S1_off_air_raw.png")))
  using(var selected=new Bitmap(Path.Combine(dir,"phones_b.png")))
  using(var oldCut=new Bitmap(Path.Combine(dir,"phones_b_cutout.png"))){
   var rawCut=new Bitmap(raw.Width,raw.Height,PixelFormat.Format32bppArgb);int rawDesk=raw.Height;
   for(int y=(int)(raw.Height*0.80);y<raw.Height;y++){bool desk=true;for(int x=0;x<raw.Width;x+=8){var c=raw.GetPixel(x,y);if(c.G>100||c.R>110||c.B>110){desk=false;break;}}if(desk){rawDesk=y;break;}}
   for(int y=0;y<rawDesk;y++)for(int x=0;x<raw.Width;x++){var c=raw.GetPixel(x,y);if(c.G>c.R+4&&c.G>c.B+4)continue;rawCut.SetPixel(x,y,Color.FromArgb(255,c.R,c.G,c.B));}
   var aligned=new Bitmap(1024,1024,PixelFormat.Format32bppArgb);
   using(var g=HQ(aligned)){g.Clear(Color.Transparent);g.DrawImage(rawCut,new Rectangle(0,0,1024,1024),new Rectangle(0,0,raw.Width,raw.Height),GraphicsUnit.Pixel);}
   var cut=(Bitmap)oldCut.Clone();var full=(Bitmap)selected.Clone();var mask=new Bitmap(1024,1024,PixelFormat.Format32bppArgb);
   int changed=0;
   for(int y=0;y<896;y++)for(int x=0;x<1024;x++){
    double w=Weight(x,y);int wi=(int)Math.Round(w*255);mask.SetPixel(x,y,Color.FromArgb(255,wi,wi,wi));
    if(w==0)continue;
    Color c=Blend(oldCut.GetPixel(x,y),aligned.GetPixel(x,y),w);cut.SetPixel(x,y,c);
    Color matte=Color.FromArgb(255,(c.R*c.A+127)/255,(c.G*c.A+255*(255-c.A)+127)/255,(c.B*c.A+127)/255);
    full.SetPixel(x,y,matte);if(matte!=selected.GetPixel(x,y))changed++;
   }
   for(int y=896;y<1024;y++)for(int x=0;x<1024;x++){cut.SetPixel(x,y,Color.Transparent);mask.SetPixel(x,y,Color.FromArgb(255,0,0,0));}
   Save(full,Path.Combine(dir,"S1_off_air.png"));Save(cut,Path.Combine(dir,"S1_off_air_cutout.png"));Save(mask,Path.Combine(dir,"S1_edit_mask.png"));
   Save(oldCut,Path.Combine(dir,"S2_on_air_cutout.png"));
   var small=new Bitmap[2];var large=new Bitmap[2];Bitmap[] sources={cut,oldCut};string[] ids={"S1_off_air","S2_on_air"};
   double round1Scale=800.0/(1085-118),round1Ty=96-118*round1Scale,nativeY=4-(77*round1Scale+round1Ty)*0.16;
   for(int k=0;k<2;k++){
    small[k]=new Bitmap(160,160,PixelFormat.Format32bppArgb);
    using(var g=HQ(small[k])){g.Clear(Color.FromArgb(22,28,43));g.DrawImage(sources[k],new RectangleF((float)(80-512*0.16),(float)nativeY,(float)(1024*0.16),(float)(1024*0.16)),new RectangleF(0,0,1024,1024),GraphicsUnit.Pixel);}
    using(var g=Graphics.FromImage(small[k]))using(var br=new SolidBrush(Color.FromArgb(42,34,48)))g.FillRectangle(br,0,138,160,22);
    Save(small[k],Path.Combine(dir,ids[k]+"_128.png"));
    large[k]=new Bitmap(640,640,PixelFormat.Format32bppArgb);
    using(var g=Graphics.FromImage(large[k])){g.InterpolationMode=InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;g.DrawImage(small[k],new Rectangle(0,0,640,640),new Rectangle(0,0,160,160),GraphicsUnit.Pixel);}
    Save(large[k],Path.Combine(dir,ids[k]+"_128_4x.png"));
   }
   var check=new Bitmap(1440,1000,PixelFormat.Format32bppArgb);
   using(var g=Graphics.FromImage(check))using(var title=new Font("Arial",25,FontStyle.Bold))using(var font=new Font("Arial",17))using(var white=new SolidBrush(Color.FromArgb(242,237,228)))using(var muted=new SolidBrush(Color.FromArgb(156,165,184))){
    g.Clear(Color.FromArgb(11,15,26));
    string[] labels={"S1 / OFF AIR","S2 / ON AIR - CORAL B"};
    for(int k=0;k<2;k++){int x=k*720;g.DrawString(labels[k],title,white,x+40,24);g.DrawString("128 px scale / native",font,muted,x+40,76);g.DrawImageUnscaled(small[k],x+280,116);g.DrawString("4x nearest-neighbor check",font,muted,x+40,294);g.DrawImageUnscaled(large[k],x+40,340);}
   }
   Save(check,Path.Combine(dir,"S_check.png"));
   string msg="{\"raw_px\":["+raw.Width+","+raw.Height+"],\"raw_desk_y\":"+rawDesk+",\"raw_normalization_scale\":"+(1024.0/raw.Width).ToString(System.Globalization.CultureInfo.InvariantCulture)+",\"translation_px\":[0,0],\"S1_changed_pixels_from_B\":"+changed+",\"S2\":\"byte-for-byte copy of selected phones_b.png\",\"output_px\":[1024,1024],\"desk_y\":896,\"preview_px\":[1440,1000]}";
   string report=Path.Combine(dir,"round2_export_metrics.json");if(File.Exists(report))throw new Exception("Report exists");File.WriteAllText(report,msg,new System.Text.UTF8Encoding(false));
   check.Dispose();for(int k=0;k<2;k++){small[k].Dispose();large[k].Dispose();}mask.Dispose();full.Dispose();cut.Dispose();aligned.Dispose();rawCut.Dispose();return msg;
  }
 }
}
"@
[NaboRound2]::Run($PSScriptRoot)

