Add-Type -AssemblyName System.Drawing
$refs=@('$PSHOME\System.Drawing.Common.dll','$PSHOME\System.Private.Windows.GdiPlus.dll','$PSHOME\System.Private.Windows.Core.dll') | ForEach-Object {$_.Replace('$PSHOME',$PSHOME)}; $refs+=@(Get-ChildItem -LiteralPath (Join-Path $PSHOME 'ref') -Filter '*.dll' | Select-Object -ExpandProperty FullName)
Add-Type -ReferencedAssemblies $refs -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
public class NaboRound1 {
 static Color HSV(double h,double s,double v) {
  h=((h%360)+360)%360; double c=v*s,x=c*(1-Math.Abs((h/60)%2-1)),m=v-c;
  double r=0,g=0,b=0;
  if(h<60){r=c;g=x;}else if(h<120){r=x;g=c;}else if(h<180){g=c;b=x;}else if(h<240){g=x;b=c;}else if(h<300){r=x;b=c;}else{r=c;b=x;}
  return Color.FromArgb(255,(int)Math.Round(255*(r+m)),(int)Math.Round(255*(g+m)),(int)Math.Round(255*(b+m)));
 }
 static void Save(Bitmap b,string p) {if(File.Exists(p))throw new Exception("Refusing overwrite: "+p); b.Save(p,ImageFormat.Png);}
 static Graphics HQ(Bitmap b){var g=Graphics.FromImage(b);g.CompositingMode=CompositingMode.SourceOver;g.CompositingQuality=CompositingQuality.HighQuality;g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.PixelOffsetMode=PixelOffsetMode.HighQuality;return g;}
 public static string Run(string dir) {
  using(var raw=new Bitmap(Path.Combine(dir,"phones_a_raw.png"))) {
   int w=raw.Width,h=raw.Height, desk=1085; var fg=new Bitmap(w,desk,PixelFormat.Format32bppArgb); var mask=new Bitmap(w,desk,PixelFormat.Format32bppArgb);
   int minx=w,miny=desk,maxx=0,maxy=0,maskCount=0,greenRemoved=0;
   for(int y=0;y<desk;y++)for(int x=0;x<w;x++){
    var c=raw.GetPixel(x,y);
    bool green=c.G>c.R+4 && c.G>c.B+4;
    if(green){fg.SetPixel(x,y,Color.Transparent);greenRemoved++;continue;}
    fg.SetPixel(x,y,Color.FromArgb(255,c.R,c.G,c.B));
    minx=Math.Min(minx,x);maxx=Math.Max(maxx,x);miny=Math.Min(miny,y);maxy=Math.Max(maxy,y);
    bool region=y<330 || (x<330 && y<710) || (x>935 && y<710);
    double hue=c.GetHue(), sat=c.GetSaturation(),v=Math.Max(c.R,Math.Max(c.G,c.B))/255.0;
    bool shell=region && hue>=24 && hue<=50 && sat>0.43 && v>0.42 && c.R>c.G && c.G>c.B;
    if(shell){mask.SetPixel(x,y,Color.White);maskCount++;}
   }
   var visited=new bool[w*desk];
   for(int sy=0;sy<desk;sy++)for(int sx=0;sx<w;sx++)if(mask.GetPixel(sx,sy).A>0 && !visited[sy*w+sx]){
    var points=new List<Point>();var queue=new Queue<Point>();queue.Enqueue(new Point(sx,sy));visited[sy*w+sx]=true;
    while(queue.Count>0){var q=queue.Dequeue();points.Add(q);int[] dx={-1,1,0,0},dy={0,0,-1,1};for(int n=0;n<4;n++){int nx=q.X+dx[n],ny=q.Y+dy[n];if(nx>=0&&nx<w&&ny>=0&&ny<desk&&!visited[ny*w+nx]&&mask.GetPixel(nx,ny).A>0){visited[ny*w+nx]=true;queue.Enqueue(new Point(nx,ny));}}}
    if(points.Count<20)foreach(var q in points){mask.SetPixel(q.X,q.Y,Color.Transparent);maskCount--;}
   }
   for(int py=155;py<305;py++)for(int px=520;px<730;px++)if(mask.GetPixel(px,py).A>0){mask.SetPixel(px,py,Color.Transparent);maskCount--;}
   var box=new Rectangle(minx,miny,maxx-minx+1,maxy-miny+1);
   Save(mask,Path.Combine(dir,"headphone_palette_mask.png"));
   string[] ids={"a","b","c"}, names={"A / AMBER","B / CORAL","C / IVORY"};
   Color[] bases={ColorTranslator.FromHtml("#ECA34D"),ColorTranslator.FromHtml("#E97967"),ColorTranslator.FromHtml("#E8E0CE")};
   var finals=new Bitmap[3];var cuts=new Bitmap[3];var native=new Bitmap[3];var large=new Bitmap[3];
   double scale=800.0/(desk-118.0), tx=512.0-w*scale/2, ty=96.0-118.0*scale;
   int[] greenEdges=new int[3];int[] unique=new int[3];int[] changed=new int[3];int[] alphaZero=new int[3],alphaFull=new int[3],alphaPartial=new int[3];
   for(int k=0;k<3;k++) {
    using(var recolor=(Bitmap)fg.Clone()){
     var target=bases[k];double th=target.GetHue(),ts=(Math.Max(target.R,Math.Max(target.G,target.B))-Math.Min(target.R,Math.Min(target.G,target.B)))/(double)Math.Max(target.R,Math.Max(target.G,target.B)),tv=Math.Max(target.R,Math.Max(target.G,target.B))/255.0;
     for(int y=0;y<desk;y++)for(int x=0;x<w;x++)if(mask.GetPixel(x,y).A>0) {
      var c=fg.GetPixel(x,y);double v=Math.Max(c.R,Math.Max(c.G,c.B))/255.0;
      recolor.SetPixel(x,y,HSV(th+(c.GetHue()-33)*0.25,Math.Min(1,((Math.Max(c.R,Math.Max(c.G,c.B))-Math.Min(c.R,Math.Min(c.G,c.B)))/(double)Math.Max(c.R,Math.Max(c.G,c.B)))*ts/0.72),Math.Min(1,v*tv/0.94)));
     }
     cuts[k]=new Bitmap(1024,1024,PixelFormat.Format32bppArgb);
     using(var g=HQ(cuts[k])){g.Clear(Color.Transparent);g.DrawImage(recolor,new RectangleF((float)tx,(float)ty,(float)(w*scale),(float)(desk*scale)),new RectangleF(0,0,w,desk),GraphicsUnit.Pixel);}
     using(var g=Graphics.FromImage(cuts[k])){g.CompositingMode=CompositingMode.SourceCopy;using(var br=new SolidBrush(Color.Transparent))g.FillRectangle(br,0,896,1024,128);}
     for(int cy=0;cy<896;cy++)for(int cx=0;cx<1024;cx++){var cc=cuts[k].GetPixel(cx,cy);if(cc.A>0&&cc.G>cc.R+4&&cc.G>cc.B+4)cuts[k].SetPixel(cx,cy,Color.FromArgb(cc.A,cc.R,Math.Max(cc.R,cc.B),cc.B));}
     Save(cuts[k],Path.Combine(dir,"phones_"+ids[k]+"_cutout.png"));
     finals[k]=new Bitmap(1024,1024,PixelFormat.Format32bppArgb);
     using(var g=Graphics.FromImage(finals[k])){g.Clear(Color.FromArgb(0,255,0));g.DrawImageUnscaled(cuts[k],0,0);using(var br=new SolidBrush(Color.FromArgb(42,34,48)))g.FillRectangle(br,0,896,1024,128);}
     Save(finals[k],Path.Combine(dir,"phones_"+ids[k]+".png"));
     native[k]=new Bitmap(160,160,PixelFormat.Format32bppArgb);
     double nativeScale=0.16, nativeY=4-(miny*scale+ty)*nativeScale;
     using(var g=HQ(native[k])){g.Clear(Color.FromArgb(22,28,43));g.DrawImage(cuts[k],new RectangleF((float)(80-512*nativeScale),(float)nativeY,(float)(1024*nativeScale),(float)(1024*nativeScale)),new RectangleF(0,0,1024,1024),GraphicsUnit.Pixel);}
     using(var g=Graphics.FromImage(native[k]))using(var br=new SolidBrush(Color.FromArgb(42,34,48)))g.FillRectangle(br,0,138,160,22);
     Save(native[k],Path.Combine(dir,"phones_"+ids[k]+"_128.png"));
     large[k]=new Bitmap(640,640,PixelFormat.Format32bppArgb);
     using(var g=Graphics.FromImage(large[k])){g.InterpolationMode=InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;g.DrawImage(native[k],new Rectangle(0,0,640,640),new Rectangle(0,0,160,160),GraphicsUnit.Pixel);}
     Save(large[k],Path.Combine(dir,"phones_"+ids[k]+"_128_4x.png"));
     var colors=new HashSet<int>();int zero=0,full=0,partial=0;
     for(int y=0;y<1024;y++)for(int x=0;x<1024;x++){
      var c=cuts[k].GetPixel(x,y);if(c.A==0)zero++;else if(c.A==255)full++;else partial++;
      if(c.A>0){colors.Add(c.ToArgb());if(c.G>c.R+4 && c.G>c.B+4)greenEdges[k]++;}
      if(k>0 && finals[k].GetPixel(x,y)!=finals[0].GetPixel(x,y))changed[k]++;
     }
     unique[k]=colors.Count;alphaZero[k]=zero;alphaFull[k]=full;alphaPartial[k]=partial;
    }
   }
   var compare=new Bitmap(2160,1000,PixelFormat.Format32bppArgb);
   using(var g=Graphics.FromImage(compare))using(var font=new Font("Arial",25,FontStyle.Bold))using(var small=new Font("Arial",17))using(var white=new SolidBrush(Color.FromArgb(242,237,228)))using(var muted=new SolidBrush(Color.FromArgb(156,165,184))) {
    g.Clear(Color.FromArgb(11,15,26));
    for(int k=0;k<3;k++){int x=k*720;g.DrawString(names[k],font,white,x+40,24);g.DrawString("128 px cap-to-desk / native",small,muted,x+40,76);g.DrawImageUnscaled(native[k],x+280,116);g.DrawString("4x nearest-neighbor check",small,muted,x+40,294);g.DrawImageUnscaled(large[k],x+40,340);}
   }
   Save(compare,Path.Combine(dir,"phones_compare.png"));compare.Dispose();
   bool[] deskOk=new bool[3];bool[] bgOk=new bool[3];bool[] opaque=new bool[3];
   for(int k=0;k<3;k++){
    deskOk[k]=bgOk[k]=opaque[k]=true;
    for(int y=0;y<1024;y++)for(int x=0;x<1024;x++){
     var c=finals[k].GetPixel(x,y);if(c.A!=255)opaque[k]=false;
     if(y>=896 && c.ToArgb()!=Color.FromArgb(42,34,48).ToArgb())deskOk[k]=false;
     if(y<896 && cuts[k].GetPixel(x,y).A==0 && c.ToArgb()!=Color.FromArgb(0,255,0).ToArgb())bgOk[k]=false;
    }
   }
   int outside=0,capChanged=0,faceChanged=0;
   for(int y=0;y<1024;y++)for(int x=0;x<1024;x++){
    bool diff=finals[0].GetPixel(x,y)!=finals[1].GetPixel(x,y)||finals[0].GetPixel(x,y)!=finals[2].GetPixel(x,y);
    if(diff && !(y<315 || x<315 || x>710))outside++;
    if(diff && x>=430 && x<590 && y>=135 && y<245)capChanged++;
    if(diff && x>=340 && x<690 && y>=460 && y<660)faceChanged++;
   }
   var sb=new System.Text.StringBuilder();
   sb.Append("{\"source_px\":["+w+","+h+"],\"source_foreground_bbox\":["+box.X+","+box.Y+","+box.Width+","+box.Height+"],\"source_desk_y\":"+desk+",\"source_cap_top_y\":118,\"scale\":"+scale.ToString(System.Globalization.CultureInfo.InvariantCulture)+",\"output_px\":[1024,1024],\"desk_y\":896,\"native_preview_px\":[160,160],\"native_cap_to_desk_px\":128,\"nearest_preview_px\":[640,640],\"compare_px\":[2160,1000],\"palette_mask_pixels\":"+maskCount+",\"keyed_background_pixels\":"+greenRemoved+",\"non_headphone_changes\":"+outside+",\"cap_logo_changed_pixels\":"+capChanged+",\"face_region_changed_pixels\":"+faceChanged+",\"variants\":[");
   for(int k=0;k<3;k++){if(k>0)sb.Append(",");sb.Append("{\"id\":\""+ids[k]+"\",\"base_hex\":\""+ColorTranslator.ToHtml(bases[k])+"\",\"all_opaque\":"+opaque[k].ToString().ToLower()+",\"pure_green_background\":"+bgOk[k].ToString().ToLower()+",\"exact_desk_color_and_y\":"+deskOk[k].ToString().ToLower()+",\"remaining_green_subject_pixels\":"+greenEdges[k]+",\"transparent_cutout_pixels\":"+alphaZero[k]+",\"opaque_cutout_pixels\":"+alphaFull[k]+",\"antialiased_cutout_pixels\":"+alphaPartial[k]+",\"rgba_colors_with_aa\":"+unique[k]+",\"changed_pixels_vs_a\":"+changed[k]+"}");}
   sb.Append("]}");
   string report=sb.ToString();
   string reportPath=Path.Combine(dir,"round1_metrics.json");if(File.Exists(reportPath))throw new Exception("Metrics exists");File.WriteAllText(reportPath,report,new System.Text.UTF8Encoding(false));
   for(int k=0;k<3;k++){finals[k].Dispose();cuts[k].Dispose();native[k].Dispose();large[k].Dispose();} fg.Dispose();mask.Dispose();
   return report;
  }
 }
}
"@
[NaboRound1]::Run($PSScriptRoot)