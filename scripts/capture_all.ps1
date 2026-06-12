. "x:\biofonia\Paisaje_sonoro\analisis_espectograma_resaltar_frecuencias\etiquetador_cpp\scripts\capture_engine.ps1"
$SP=" "   # espacio
# --- VISTAS (1-7) ---
Run-Fig "man_v1_espectro2d.png"      @("key:1","wait:800")
Run-Fig "man_v2_terreno3d.png"       @("key:2","wait:800")
Run-Fig "man_v3_rio_espectral.png"   @("key:3","wait:800")
Run-Fig "man_v4_nube_puntos.png"     @("key:4","wait:800")
Run-Fig "man_v5_cascada_espectral.png" @("key:5","wait:800")
Run-Fig "man_v6_quiver3d.png"        @("key:6","wait:800")
Run-Fig "man_v7_volumen.png"         @("key:a","wait:1600","key:7","wait:800")
# --- HERRAMIENTAS DE ETIQUETADO ---
Run-Fig "man_tool_selec.png"   @("key:a","wait:1600","key:S","click:0.37,0.44")
Run-Fig "man_tool_bbox.png"    @("key:Y","drag:0.3,0.25,0.7,0.45")
Run-Fig "man_tool_poly.png"    @("key:P","click:0.35,0.3","click:0.65,0.3","click:0.6,0.5","click:0.4,0.52")
Run-Fig "man_tool_edit.png"    @("key:a","wait:1600","key:E","click:0.37,0.44")
Run-Fig "man_tool_cut.png"     @("key:a","wait:1600","key:X","drag:0.45,0.28,0.55,0.5")
Run-Fig "man_tool_auto.png"    @("key:a","wait:1800")
Run-Fig "man_tool_autosel.png" @("drag:0.3,0.25,0.7,0.5")
Run-Fig "man_tool_mejorar.png" @("key:a","wait:1600","key:S","click:0.485,0.70")
Run-Fig "man_tool_nueva.png"   @("key:N","key:a","key:v","key:e")
Run-Fig "man_tool_ocultar.png" @("key:a","wait:1600","key:O")
Run-Fig "man_tool_limpiar.png" @("key:a","wait:1600","key:c")
# --- VISUALIZACION / DISPLAY ---
Run-Fig "man_feat_cursor.png"     @("move:0.5,0.3")
Run-Fig "man_feat_mapa_combo.png" @("key:M","wait:300")
Run-Fig "man_feat_mapa_grises.png" @("key:M","vkey:0x28","vkey:0x28","vkey:0x28","vkey:0x28","vkey:0x0D")
Run-Fig "man_feat_histograma.png" @("key:I","wait:300")
Run-Fig "man_feat_senal.png"      @("key:f","wait:200")
Run-Fig "man_feat_filtro_freq.png" (@("key:j")*7 + @("wait:400"))
Run-Fig "man_feat_filtro_db.png"   (@("key:y")*30 + @("wait:300"))
Run-Fig "man_feat_filtro_vol.png"  @("drag:0.985,0.5,0.985,0.72","wait:200")
# --- REPRODUCCION / NAVEGACION ---
Run-Fig "man_feat_play.png"       @("key:$SP","wait:700")
Run-Fig "man_feat_velocidad.png"  @("key:v","key:v","key:v","key:v","key:v","wait:200")
Run-Fig "man_feat_banda.png"      @("drag:0.3,0.25,0.7,0.45","key:B","wait:200")
Run-Fig "man_feat_zoom2d.png"     @("key:Z","wait:250","key:Z","wait:300")
Run-Fig "man_feat_resolucion.png" @("key:]","wait:1600","key:]","wait:1600")
Run-Fig "man_feat_navegador.png"  @("wait:300")
# --- GUARDAR / FLUJO ---
Run-Fig "man_flujo_guardar.png"   @("key:a","wait:1600","key:s","wait:400")
"=== TODAS LAS FIGURAS CAPTURADAS ==="
