from PIL import Image, ImageDraw, ImageFont, ImageEnhance
import os
import logging
from datetime import datetime

logger = logging.getLogger(__name__)

# Constants
DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480

def get_font_path(bold=True):
    # This might need to be adjusted or we can pass font paths in
    # For now, we reuse the logic or hardcode paths relative to project
    base_dir = os.path.dirname(os.path.abspath(__file__))
    # Fonts from user reference
    fonts = [
        # Local Project Root (Best Match)
        os.path.join(base_dir, "AppleSDGothicNeoB.ttf"), 
        # User specific local paths
        "/usr/local/share/fonts/apple_sandol/AppleSDGothicNeoEB.ttf" if bold else "/usr/local/share/fonts/apple_sandol/AppleSDGothicNeoB.ttf",
        "/usr/local/share/fonts/apple_sandol/AppleSDGothicNeoM.ttf",
        # Project local fallback (User's subdir)
        os.path.join(base_dir, "Apple_산돌고딕_Neo", "AppleSDGothicNeoEB.ttf" if bold else "AppleSDGothicNeoB.ttf"),
        # Standard System Fonts
        "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf" if bold else "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc" if bold else "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         # Generic Fallbacks
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
    ]
    for f in fonts:
        if os.path.exists(f): return f
    return None

def get_font(size=20, bold=True):
    path = get_font_path(bold)
    if path:
        return ImageFont.truetype(path, size)
    return ImageFont.load_default()

def resize_image_fill(image, width=DISPLAY_WIDTH, height=DISPLAY_HEIGHT):
    target_ratio = width / height
    img_ratio = image.width / image.height
    
    if img_ratio > target_ratio:
        # Image is wider than display: Resize by height, crop width
        new_height = height
        new_width = int(image.width * (new_height / image.height))
    else:
        # Image is taller than display: Resize by width, crop height
        new_width = width
        new_height = int(image.height * (new_width / image.width))
        
    resized = image.resize((new_width, new_height), Image.Resampling.LANCZOS)
    
    # Simple Center Crop
    left = (new_width - width) / 2
    top = (new_height - height) / 2
    
    return resized.crop((left, top, left + width, top + height))

def enhance_image(image):
    if image.mode != 'RGB': image = image.convert('RGB')
    image = ImageEnhance.Contrast(image).enhance(1.2)
    image = ImageEnhance.Sharpness(image).enhance(1.5)
    return ImageEnhance.Color(image).enhance(1.1)

def get_dust_grade_info(pm10, pm25):
    try:
        p10, p25 = int(pm10 or -1), int(pm25 or -1)
    except:
        return "정보 없음", "●", (128, 128, 128)

    lv = max(
        1 if p25 <= 15 else 2 if p25 <= 35 else 3 if p25 <= 75 else 4,
        1 if p10 <= 30 else 2 if p10 <= 80 else 3 if p10 <= 150 else 4
    )
    return ["", "좋음", "보통", "나쁨", "매우나쁨"][lv], "●", \
    [(0, 0, 0), (0, 0, 255), (0, 128, 0), (255, 165, 0), (255, 0, 0)][lv]

def _load_weather_icons(icon_dir):
    icons = {}
    icon_map = {'맑음': 'sun.png', '구름 많음': 'cloud.png', '흐림': 'cloudy.png', '비': 'rain.png',
                '비 또는 눈': 'rain_snow.png', '눈': 'snow.png', '소나기': 'shower.png', '정보없음': 'unknown.png'}
    if not os.path.exists(icon_dir): return icons
    for desc, filename in icon_map.items():
        path = os.path.join(icon_dir, filename)
        if os.path.exists(path):
            try:
                icons[desc] = Image.open(path).convert("RGBA").resize((45, 45), Image.Resampling.LANCZOS)
            except:
                pass
    return icons

def create_composed_image(image_path, weather_data, dust_data, layout_config=None, location_name="위치 미설정", batt_info=None):
    # Defaults
    if layout_config is None: layout_config = {}
    
    # Load Image
    try:
        if image_path and os.path.exists(image_path):
            img = Image.open(image_path)
        else:
            img = Image.new('RGB', (DISPLAY_WIDTH, DISPLAY_HEIGHT), (200, 200, 200))
    except:
        img = Image.new('RGB', (DISPLAY_WIDTH, DISPLAY_HEIGHT), (200, 200, 200))
        
    img = resize_image_fill(img)
    img = enhance_image(img)
    
    # Overlay
    overlay = Image.new('RGBA', (DISPLAY_WIDTH, DISPLAY_HEIGHT), (255, 255, 255, 0))
    draw = ImageDraw.Draw(overlay)

    # --- [Apple-like Vertical Widget Layout] ---
    widget_scale = float(layout_config.get('widget_size', 1.0))
    font_scale = float(layout_config.get('font_scale', 1.0)) # Font Slider
    opacity_val = float(layout_config.get('opacity', 0.85))
    bg_alpha = int(255 * opacity_val)

    # Base Dimensions
    card_w = int(220 * widget_scale) 
    padding = int(15 * widget_scale)
    line_spacing = int(5 * widget_scale)

    # Fonts
    s = widget_scale * font_scale
    font_xl = get_font(int(34 * s)) # User Req: Reduce Font Size (45->34)
    font_lg = get_font(int(22 * s)) 
    font_md = get_font(int(16 * s)) 
    font_sm = get_font(int(13 * s)) 

    # Load Icons
    base_dir = os.path.dirname(os.path.abspath(__file__))
    icon_dir = os.path.join(base_dir, 'icons')
    weather_icons = _load_weather_icons(icon_dir)

    # Data Preparation
    temp_str = "--°"
    desc_str = ""
    rain_str = ""
    umbrella_msg = ""
    w_icon = None

    if weather_data and 'temp' in weather_data: # Changed w_data to weather_data
        temp_str = f"{int(weather_data['temp'])}°"
        desc_str = weather_data.get('weather_description', '정보없음')
        
        # Icon
        w_icon = weather_icons.get(desc_str, weather_icons.get('정보없음'))
        
        if w_icon:
            icon_sz = int(55 * widget_scale)
            if w_icon.size[0] != icon_sz:
                w_icon = w_icon.resize((icon_sz, icon_sz), Image.Resampling.LANCZOS)

        # Current Rain
        if weather_data.get('current_rain_amount', 0) > 0:
            rain_str = f"강수 {weather_data['current_rain_amount']:.1f}mm"
            
        # [Rain Widget handled separately]


    # Dust (2 Lines)
    pm10_str = "미세먼지 --"
    pm25_str = "초미세 --"
    color_pm10 = (150,150,150)
    color_pm25 = (150,150,150)

    if dust_data: # Changed d_data to dust_data
        p10 = dust_data.get('pm10')
        p25 = dust_data.get('pm25')
        
        # Helper to safely format value
        def fmt_dust(val):
            return f"{val}" if val is not None else "--"
            
        g10, _, c10 = get_dust_grade_info(p10, 0)
        g25, _, c25 = get_dust_grade_info(0, p25)
        
        pm10_str = f"미세먼지 {fmt_dust(p10)}"
        pm25_str = f"초미세 {fmt_dust(p25)}"
        
        # Override color if None
        if p10 is None: c10 = (150, 150, 150)
        if p25 is None: c25 = (150, 150, 150)
        
        color_pm10 = c10
        color_pm25 = c25

    # Time
    now = datetime.now()
    # User Req: "12/18 05:30 기준" format
    time_str = now.strftime('%m/%d %H:%M 기준')
    
    # Battery String (Optional in Main Widget, typically not shown but can be added if needed)
    # User only requested Popup.

    # --- Layout Calculation ---
    current_y = padding
    
    # Row 1
    h_row1 = max(w_icon.height if w_icon else 0, font_xl.getbbox(temp_str)[3])
    current_y += h_row1 + line_spacing
    
    # Row 2
    current_y += font_lg.getbbox(desc_str)[3] + (line_spacing * 2)
    
    # Row 3 (Dust 1)
    current_y += font_md.getbbox(pm10_str)[3] + line_spacing
    # Row 4 (Dust 2)
    current_y += font_md.getbbox(pm25_str)[3] + (line_spacing * 2)
    
    # Row 5 (Umbrella removed)
    # if umbrella_msg:
    #     current_y += font_sm.getbbox(umbrella_msg)[3] + line_spacing


    # Row 6 (Time)
    current_y += 5 + font_sm.getbbox(time_str)[3] + padding

    box_h = int(current_y) # Dynamic Height
    box_w = card_w

    # Position (Right-Top Anchor Logic)
    # User Req: Top-Right fixed, 20px padding (fixed), Expand Left/Down
    
    pos_x = layout_config.get('x')
    pos_y = layout_config.get('y')
    layout_type = layout_config.get('type', 'type_A')
    
    # Defaults
    margin = 20 # Fixed 20px
    
    # Calculate Anchor Point (Top-Right)
    # box_x is calculated so that (box_x + box_w) is at (DISPLAY_WIDTH - margin)
    box_x = DISPLAY_WIDTH - box_w - margin
    box_y = margin

    if layout_type == 'custom':
        if pos_x is not None: box_x = int(float(pos_x))
        if pos_y is not None: box_y = int(float(pos_y))
    elif layout_config.get('position') == 'bottom' or layout_type == 'type_B':
        box_y = DISPLAY_HEIGHT - box_h - margin

    # Overflow Protection
    if box_x + box_w > DISPLAY_WIDTH: box_x = DISPLAY_WIDTH - box_w
    if box_x < 0: box_x = 0
    if box_y + box_h > DISPLAY_HEIGHT: box_y = DISPLAY_HEIGHT - box_h
    if box_y < 0: box_y = 0

    # Draw Background
    draw.rounded_rectangle([box_x, box_y, box_x + box_w, box_y + box_h], 
                           radius=int(18 * widget_scale), 
                           fill=(255, 255, 255, bg_alpha), 
                           outline=None)
    
    cx = box_x + padding
    cy = box_y + padding

    # Row 1: Icon + Temp
    if w_icon:
        # Paste with alpha
        if w_icon.mode != 'RGBA': w_icon = w_icon.convert('RGBA')
        overlay.paste(w_icon, (cx, cy), w_icon) # Changed base_img to overlay
        
        temp_x = cx + w_icon.width + 10
        temp_y = cy + (w_icon.height - font_xl.getbbox(temp_str)[3]) // 2 - 5
        draw.text((temp_x, temp_y), temp_str, font=font_xl, fill=(0,0,0))
        cy += max(w_icon.height, font_xl.getbbox(temp_str)[3]) + line_spacing
    else:
        draw.text((cx, cy), temp_str, font=font_xl, fill=(0,0,0))
        cy += font_xl.getbbox(temp_str)[3] + line_spacing

    # Row 2: Desc
    draw.text((cx + 5, cy), desc_str, font=font_lg, fill=(50,50,50))
    cy += font_lg.getbbox(desc_str)[3] + (line_spacing * 2)

    # Row 3: Dust PM10
    dot_r = int(5 * widget_scale)
    draw.text((cx + 5, cy), pm10_str, font=font_md, fill=(60,60,60))
    txt_w = font_md.getlength(pm10_str)
    dot_cx = cx + 5 + txt_w + 15
    dot_cy = cy + (font_md.getbbox("A")[3] // 2) + 2
    draw.ellipse([dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r], fill=color_pm10)
    cy += font_md.getbbox(pm10_str)[3] + line_spacing

    # Row 4: Dust PM2.5
    draw.text((cx + 5, cy), pm25_str, font=font_md, fill=(60,60,60))
    txt_w = font_md.getlength(pm25_str)
    dot_cx = cx + 5 + txt_w + 15
    dot_cy = cy + (font_md.getbbox("A")[3] // 2) + 2
    draw.ellipse([dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r], fill=color_pm25)
    cy += font_md.getbbox(pm25_str)[3] + (line_spacing * 2)

    # Row 5: Umbrella (Removed)
    # if umbrella_msg:
    #     draw.text((cx + 5, cy), umbrella_msg, font=font_sm, fill=(0,0,200))
    #     cy += font_sm.getbbox(umbrella_msg)[3] + line_spacing


    # Row 6: Time
    draw.text((cx + 5, cy), time_str, font=font_sm, fill=(120,120,120))

    final_image = Image.alpha_composite(img.convert('RGBA'), overlay).convert('RGB')
    
    # [Conversational Summary Widget Composite]
    if weather_data:
        summary_widget = create_daily_summary_widget(weather_data)
        if summary_widget:
            rx = 20 
            ry = DISPLAY_HEIGHT - 36 - 15
            final_image.paste(summary_widget, (rx, ry), summary_widget)

    # [Top-Left Compact Battery Indicator]
    if batt_info:
        level = batt_info.get('level', 100)
        charging = batt_info.get('charging', False)
        batt_icon = "⚡" if charging else ("🪫" if level < 20 else "🔋")
        batt_text = f"{batt_icon} {int(level)}%"
        
        font_batt = get_font(12, bold=True)
        bw_text = font_batt.getlength(batt_text)
        bh_text = font_batt.getbbox(batt_text)[3]
        
        bw = int(bw_text + 16)
        bh = 24
        bx, by = 20, 20
        
        batt_overlay = Image.new('RGBA', (DISPLAY_WIDTH, DISPLAY_HEIGHT), (255, 255, 255, 0))
        bdraw = ImageDraw.Draw(batt_overlay)
        bdraw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=10, fill=(255, 255, 255, 180))
        bdraw.text((bx + 8, by + (bh - bh_text) // 2 - 1), batt_text, font=font_batt, fill=(40, 40, 40))
        final_image = Image.alpha_composite(final_image.convert('RGBA'), batt_overlay).convert('RGB')

    return final_image, box_x, box_y, box_w, box_h

def create_daily_summary_widget(weather_data):
    """
    Creates a compact, slim bottom widget for daily summary and rain alerts.
    """
    if not weather_data:
        return None
        
    max_temp = weather_data.get('max_temp')
    pop = weather_data.get('pop')
    rain_info = weather_data.get('rain_forecast')
    
    if max_temp is None and pop is None and not rain_info:
        return None
        
    # Message Construction
    parts = []
    if max_temp is not None:
        parts.append(f"최고 {int(max_temp)}°C")
    if pop is not None:
        parts.append(f"강수확률 {int(pop)}%")
        
    base_msg = "오늘은 " + ", ".join(parts) + "예요." if parts else ""
    
    if rain_info:
        start_h = rain_info['start_time'].split(':')[0]
        r_list = ["", "비", "비/눈", "눈", "소나기"]
        rtype = r_list[rain_info.get('type_code', 1)] if rain_info.get('type_code', 1) < len(r_list) else "비"
        rain_msg = f"{start_h}시부터 {rtype}가 오니 우산 챙기세요!"
        if base_msg:
            message = f"💡 {base_msg} {rain_msg}"
        else:
            message = f"☔️ {rain_msg}"
    else:
        message = f"🌤️ {base_msg}" if base_msg else ""
        
    if not message:
        return None
        
    # Compact Dimensions (50% 축소된 슬림 알약 배너)
    font_small = get_font(14)
    text_w = font_small.getlength(message)
    text_h = font_small.getbbox(message)[3]
    
    w_h = 36
    w_w = int(text_w + 30)
    
    img = Image.new('RGBA', (w_w, w_h), (255, 255, 255, 0))
    draw = ImageDraw.Draw(img)
    
    # Background (Glass/White Pill)
    draw.rounded_rectangle([0, 0, w_w, w_h], radius=12, fill=(255, 255, 255, 200))
    
    tx = (w_w - text_w) / 2
    ty = (w_h - text_h) / 2 - 2
    
    # Draw Text (Dark Blue for clean readability)
    draw.text((tx, ty), message, font=font_small, fill=(20, 40, 80))
    
    return img

def create_battery_alert_widget(batt_info):
    """
    Creates a popup if battery is < 15% and NOT charging
    """
    level = batt_info.get('level', 100)
    charging = batt_info.get('charging', False)
    
    if level >= 15 or charging:
        return None
        
    message = f"🪫 배터리 부족 ({int(level)}%), 충전해 주세요!"
    
    w_h = 100
    w_w = DISPLAY_WIDTH - 40
    
    img = Image.new('RGBA', (w_w, w_h), (255, 255, 255, 0))
    draw = ImageDraw.Draw(img)
    
    # Background (Red tint/White)
    draw.rounded_rectangle([0, 0, w_w, w_h], radius=30, fill=(255, 240, 240, 240)) # Light Red Hue
    
    font_large = get_font(40)
    text_w = font_large.getlength(message)
    text_h = font_large.getbbox(message)[3]
    
    tx = (w_w - text_w) / 2
    ty = (w_h - text_h) / 2 - 5
    
    # Red Warning Text
    draw.text((tx, ty), message, font=font_large, fill=(200, 0, 0))
    
    return img

