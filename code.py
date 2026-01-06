import board
import busio
import displayio
import vectorio
import math
import usb_cdc
import terminalio
import time
from adafruit_display_text import label
from adafruit_st7789 import ST7789
from fourwire import FourWire

WIDTH = 240
HEIGHT = 240
ROTATION = 180 #Set to 0 if you use the screen on the other side

RADAR_Y_START = 5 
RADAR_HEIGHT = 150 
RADAR_Y_END = RADAR_Y_START + RADAR_HEIGHT
RADAR_CX = WIDTH // 2

RADAR_CY = RADAR_Y_START + (RADAR_HEIGHT // 2) + 50

LIST_Y_START = 160
ROW_HEIGHT = 15 

C_BLACK = 0x000000
C_WHITE = 0xFFFFFF
C_GREY  = 0x333333
C_GREEN = 0x00FF00
C_RED   = 0xFF0000
C_YELLOW = 0xFFFF00
SLOT_COLORS = [0xFF0000, 0xFFFF00, 0x00FFFF, 0xD2691E, 0xFFFFFF]

displayio.release_displays()
spi = busio.SPI(clock=board.GPIO12, MOSI=board.GPIO11)
display_bus = FourWire(spi, command=board.GPIO8, chip_select=board.GPIO10, reset=board.GPIO9, baudrate=60000000)
display = ST7789(display_bus, width=WIDTH, height=HEIGHT, rotation=ROTATION, rowstart=80, colstart=0)
display.auto_refresh = False

main_group = displayio.Group()
waiting_group = displayio.Group()

wait_bg_pal = displayio.Palette(1)
wait_bg_pal[0] = C_BLACK
wait_bg = vectorio.Rectangle(pixel_shader=wait_bg_pal, width=WIDTH, height=HEIGHT, x=0, y=0)
waiting_group.append(wait_bg)

lbl_wait = label.Label(terminalio.FONT, text="WAITING FOR\nPROGRAM...", color=C_GREEN, scale=2)
lbl_wait.anchor_point = (0.5, 0.5)
lbl_wait.anchored_position = (WIDTH // 2, HEIGHT // 2)
waiting_group.append(lbl_wait)

bg_pal = displayio.Palette(1)
bg_pal[0] = C_BLACK
main_group.append(vectorio.Rectangle(pixel_shader=bg_pal, width=WIDTH, height=HEIGHT, x=0, y=0))

border_pal = displayio.Palette(1)
border_pal[0] = C_WHITE
main_group.append(vectorio.Rectangle(pixel_shader=border_pal, width=WIDTH, height=2, x=0, y=RADAR_Y_START))
main_group.append(vectorio.Rectangle(pixel_shader=border_pal, width=WIDTH, height=2, x=0, y=RADAR_Y_END))
main_group.append(vectorio.Rectangle(pixel_shader=border_pal, width=2, height=RADAR_HEIGHT, x=0, y=RADAR_Y_START))
main_group.append(vectorio.Rectangle(pixel_shader=border_pal, width=2, height=RADAR_HEIGHT, x=WIDTH-2, y=RADAR_Y_START))

pl_pal = displayio.Palette(1)
pl_pal[0] = C_GREEN
main_group.append(vectorio.Circle(pixel_shader=pl_pal, radius=3, x=RADAR_CX, y=RADAR_CY))

lbl_site = label.Label(terminalio.FONT, text="?", color=C_YELLOW, scale=1)
lbl_site.anchor_point = (0.5, 0.0) 
lbl_site.anchored_position = (RADAR_CX, RADAR_Y_START + 5)
lbl_site.hidden = True
main_group.append(lbl_site)

class EnemySlot:
    def __init__(self, index):
        self.index = index
        self.color = SLOT_COLORS[index]
        self.assigned_name = None
        self.group = displayio.Group()
        self.last_x = -1
        self.last_y = -1
        self.last_hp = -1
        self.last_name = ""
        self.last_hidden = True
        self.last_dot_hidden = True
        
        self.dot_pal = displayio.Palette(1)
        self.dot_pal[0] = self.color
        self.dot = vectorio.Circle(pixel_shader=self.dot_pal, radius=3, x=0, y=0)
        
        base_y = LIST_Y_START + (index * ROW_HEIGHT)
        self.lbl = label.Label(terminalio.FONT, text="", color=self.color, scale=1)
        self.lbl.anchor_point = (0.0, 0.5)
        self.lbl.anchored_position = (5, base_y + 6)
        
        self.bar_bg_pal = displayio.Palette(1)
        self.bar_bg_pal[0] = C_GREY
        self.bar_bg = vectorio.Rectangle(pixel_shader=self.bar_bg_pal, width=80, height=6, x=150, y=base_y+3)
        
        self.bar_fg_pal = displayio.Palette(1)
        self.bar_fg_pal[0] = C_GREEN
        self.bar_fg = vectorio.Rectangle(pixel_shader=self.bar_fg_pal, width=80, height=6, x=150, y=base_y+3)
        
        self.strike_pal = displayio.Palette(1)
        self.strike_pal[0] = C_RED
        self.strike = vectorio.Rectangle(pixel_shader=self.strike_pal, width=1, height=2, x=0, y=0)
        self.strike.hidden = True
        
        self.group.append(self.dot)
        self.group.append(self.bar_bg)
        self.group.append(self.bar_fg)
        self.group.append(self.lbl)
        self.group.append(self.strike) 
        self.group.hidden = True
        main_group.append(self.group)

    def update(self, x, y, hp, name):
        if self.last_hidden:
            self.group.hidden = False
            self.last_hidden = False
        
        self.assigned_name = name
        
        dot_should_hide = not (2 < x < WIDTH-2 and RADAR_Y_START < y < RADAR_Y_END-2)
        
        if not dot_should_hide:
            if self.last_x != x or self.last_y != y:
                self.dot.x = x
                self.dot.y = y
                self.last_x = x
                self.last_y = y
            if self.last_dot_hidden:
                self.dot.hidden = False
                self.last_dot_hidden = False
        else:
            if not self.last_dot_hidden:
                self.dot.hidden = True
                self.last_dot_hidden = True
        
        if self.last_name != name:
            self.lbl.text = name[:10]
            self.last_name = name
            
        if not self.strike.hidden:
            self.strike.hidden = True
            self.lbl.color = self.color
        
        if self.last_hp != hp:
            if hp > 0:
                pct = hp / 100.0
                w = int(80 * pct)
                self.bar_fg.width = max(1, w)
                new_color = C_GREEN if hp > 30 else C_RED
                if self.bar_fg_pal[0] != new_color:
                    self.bar_fg_pal[0] = new_color
                if self.bar_bg.hidden:
                    self.bar_bg.hidden = False
                    self.bar_fg.hidden = False
            elif hp <= 0 and self.last_hp > 0:
                self.mark_dead()
            self.last_hp = hp

    def mark_dead(self):
        if self.last_hidden:
            self.group.hidden = False
            self.last_hidden = False
        if not self.last_dot_hidden:
            self.dot.hidden = True
            self.last_dot_hidden = True
        if not self.bar_bg.hidden:
            self.bar_bg.hidden = True
            self.bar_fg.hidden = True
        txt_w = len(self.lbl.text) * 6 
        if txt_w < 1: txt_w = 1 
        self.strike.width = txt_w + 4
        self.strike.x = self.lbl.x - 2
        self.strike.y = self.lbl.y - 1
        if self.strike.hidden:
            self.strike.hidden = False
        if self.lbl.color != C_GREY:
            self.lbl.color = C_GREY
        self.last_hp = 0

slots = [EnemySlot(i) for i in range(5)]

serial = usb_cdc.console

scale = (WIDTH / 2) / 2100.0 

last_refresh = time.monotonic()
min_frame_time = 1.0 / 60.0
needs_refresh = False
is_connected = False

display.root_group = waiting_group
display.refresh()

while True:
    now = time.monotonic()
    
    if serial.in_waiting:
        try:
            if serial.in_waiting > 150: 
                serial.read(serial.in_waiting)
            
            line = serial.readline()
            if not line: continue
            
            text = line.decode('utf-8').strip()
            parts = text.split(';')
            
            if parts[0].startswith('p'):
                if not is_connected:
                    is_connected = True
                    display.root_group = main_group
                    needs_refresh = True

                p_chunk = parts[0].split(',')
                px, py, ang = float(p_chunk[1]), float(p_chunk[2]), float(p_chunk[3])
                
                rad = math.radians(ang)
                si, co = math.sin(rad), math.cos(rad)
                
                seen_names = []
                bomb_planted = False
                
                for i in range(1, len(parts)):
                    c = parts[i].split(',')
                    ctype = c[0]
                    
                    if ctype == 'e' and len(c) >= 5:
                        ex, ey = float(c[1]), float(c[2])
                        hp = int(c[3])
                        name = c[4]
                        seen_names.append(name)
                        
                        dx = ex - px
                        dy = ey - py
                        fwd = dx * co + dy * si
                        rgt = dx * si - dy * co
                        
                        sx = int(RADAR_CX + (rgt * scale))
                        sy = int(RADAR_CY - (fwd * scale))
                        
                        assigned_slot = None
                        for s in slots:
                            if s.assigned_name == name:
                                assigned_slot = s
                                break
                        if not assigned_slot:
                            for s in slots:
                                if s.assigned_name is None:
                                    assigned_slot = s
                                    break
                        if assigned_slot:
                            assigned_slot.update(sx, sy, hp, name)
                            needs_refresh = True

                    elif ctype == 'b' and len(c) >= 2:
                        site_idx = int(c[1])
                        
                        #This may work on most of the maps, but not all, so... push a fix if you want
                        site_char = "B" if site_idx == 1 else "A"
                        
                        if lbl_site.text != site_char:
                            lbl_site.text = site_char
                        
                        if lbl_site.hidden:
                            lbl_site.hidden = False
                        
                        bomb_planted = True
                        needs_refresh = True

                if not bomb_planted:
                    if not lbl_site.hidden:
                        lbl_site.hidden = True
                        needs_refresh = True

                for s in slots:
                    if s.assigned_name is not None:
                        if s.assigned_name not in seen_names:
                            s.mark_dead()
                            needs_refresh = True
                            
        except Exception:
            pass 

    if needs_refresh and (now - last_refresh >= min_frame_time):
        display.refresh()
        last_refresh = now
        needs_refresh = False
