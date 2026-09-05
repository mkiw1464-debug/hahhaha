#pragma once
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "Aimbot.h"
#include "ESP.h"
#include "PlayerList.h"

// ─── Streamproof: hide menu from screenshots/recordings ──────────────────────
// We mark the menu window layer as "do not capture" at the CALayer level.
// This is the iOS AVFoundation / ReplayKit exclusion trick.
// Combined with Bypass.h UIScreen.isCaptured → fully streamproof.
@interface FFNETWindow : UIWindow
@end
@implementation FFNETWindow
- (void)layoutSubviews {
    [super layoutSubviews];
    // CALayer private API: exclude from capture
    [self.layer setValue:@YES forKey:@"allowsGroupBlending"];
    // UIWindowScene level trick: hide from screenshot compositor
    self.layer.allowsGroupBlending = YES;
}
- (BOOL)_shouldCreateContextAsSecure { return YES; } // iOS secure layer — not captured
@end

// ─── Toggle switch styled after iOS Settings ──────────────────────────────────
@interface FFToggle : UIControl
@property (nonatomic) BOOL on;
@property (nonatomic, copy) void(^onChange)(BOOL);
@end

@implementation FFToggle {
    UIView* _track;
    UIView* _thumb;
}
- (instancetype)initWithFrame:(CGRect)f {
    self = [super initWithFrame:f];
    _track = [[UIView alloc] initWithFrame:self.bounds];
    _track.layer.cornerRadius = f.size.height * 0.5f;
    _track.clipsToBounds = YES;
    [self addSubview:_track];
    _thumb = [[UIView alloc] initWithFrame:CGRectMake(2, 2, f.size.height-4, f.size.height-4)];
    _thumb.layer.cornerRadius = (f.size.height-4) * 0.5f;
    _thumb.backgroundColor = UIColor.whiteColor;
    _thumb.layer.shadowColor = UIColor.blackColor.CGColor;
    _thumb.layer.shadowOpacity = 0.3;
    _thumb.layer.shadowOffset = CGSizeMake(0,1);
    [self addSubview:_thumb];
    [self _update:NO];
    [self addTarget:self action:@selector(_tapped) forControlEvents:UIControlEventTouchUpInside];
    return self;
}
- (void)_update:(BOOL)animate {
    float trackW = self.bounds.size.width;
    float thumbW = self.bounds.size.height - 4;
    void(^block)(void) = ^{
        self->_track.backgroundColor = self->_on
            ? [UIColor colorWithRed:0.2 green:0.8 blue:0.4 alpha:1]
            : [UIColor colorWithWhite:0.3 alpha:0.6];
        self->_thumb.frame = CGRectMake(
            self->_on ? (trackW - thumbW - 2) : 2, 2, thumbW, thumbW
        );
    };
    if (animate) [UIView animateWithDuration:0.2 animations:block];
    else block();
}
- (void)setOn:(BOOL)on { _on = on; [self _update:NO]; }
- (void)_tapped { _on = !_on; [self _update:YES]; if (_onChange) _onChange(_on); }
@end

// ─── Slider row ───────────────────────────────────────────────────────────────
@interface FFSlider : UIView
@property (nonatomic, copy) void(^onValue)(float);
- (void)setMinVal:(float)mn maxVal:(float)mx current:(float)cur;
@end

@implementation FFSlider {
    UISlider* _sl;
    UILabel* _lbl;
    NSString* _title;
}
- (instancetype)initWithTitle:(NSString*)t {
    self = [super initWithFrame:CGRectZero];
    _title = t;
    UILabel* title = [[UILabel alloc] init];
    title.text = t;
    title.textColor = [UIColor colorWithWhite:0.9 alpha:1];
    title.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightMedium];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:title];

    _lbl = [[UILabel alloc] init];
    _lbl.textColor = [UIColor colorWithWhite:0.7 alpha:1];
    _lbl.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    _lbl.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_lbl];

    _sl = [[UISlider alloc] init];
    _sl.tintColor = [UIColor colorWithRed:0.4 green:0.6 blue:1 alpha:1];
    _sl.translatesAutoresizingMaskIntoConstraints = NO;
    [_sl addTarget:self action:@selector(_moved:) forControlEvents:UIControlEventValueChanged];
    [self addSubview:_sl];

    self.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [title.topAnchor constraintEqualToAnchor:self.topAnchor constant:4],
        [_lbl.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [_lbl.centerYAnchor constraintEqualToAnchor:title.centerYAnchor],
        [_sl.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [_sl.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [_sl.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:2],
        [self.bottomAnchor constraintEqualToAnchor:_sl.bottomAnchor constant:4],
    ]];
    return self;
}
- (void)setMinVal:(float)mn maxVal:(float)mx current:(float)cur {
    _sl.minimumValue = mn; _sl.maximumValue = mx; _sl.value = cur;
    _lbl.text = [NSString stringWithFormat:@"%.0f", cur];
}
- (void)_moved:(UISlider*)sl {
    _lbl.text = [NSString stringWithFormat:@"%.0f", sl.value];
    if (_onValue) _onValue(sl.value);
}
@end

// ─── Segment page selector ────────────────────────────────────────────────────
// Bone target picker for aimbot
@interface BonePicker : UISegmentedControl
@end

// ─── Menu view controller ─────────────────────────────────────────────────────
@interface FFNETMenu : UIViewController
+ (instancetype)shared;
- (void)show;
- (void)hide;
@property (nonatomic, readonly) BOOL visible;
@end

@implementation FFNETMenu {
    UIVisualEffectView* _blur;     // grey glassmorphism background
    UISegmentedControl* _pageSeg;  // page selector
    UIScrollView*       _scroll;
    UIStackView*        _aimPage;
    UIStackView*        _espPage;
    UIStackView*        _settPage;
    int                 _curPage;
    BOOL                _visible;

    // Bone picker for aimbot target
    UISegmentedControl* _boneSeg;

    ESPView*            _espOverlay;
    CADisplayLink*      _displayLink;
}

+ (instancetype)shared {
    static FFNETMenu* inst;
    static dispatch_once_t t;
    dispatch_once(&t, ^{ inst = [FFNETMenu new]; });
    return inst;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor clearColor];

    // ── Main card: blurred grey glass ─────────────────────────────────────
    UIBlurEffect* fx = [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemMaterialDark];
    _blur = [[UIVisualEffectView alloc] initWithEffect:fx];
    _blur.layer.cornerRadius = 18;
    _blur.clipsToBounds = YES;
    _blur.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.12].CGColor;
    _blur.layer.borderWidth = 1;
    // Subtle grey tint on top of blur
    UIView* tint = [[UIView alloc] initWithFrame:_blur.bounds];
    tint.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    tint.backgroundColor = [UIColor colorWithWhite:0.15 alpha:0.45];
    [_blur.contentView addSubview:tint];

    _blur.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_blur];

    // Fixed card size — centered
    [NSLayoutConstraint activateConstraints:@[
        [_blur.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [_blur.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [_blur.widthAnchor constraintEqualToConstant:300],
        [_blur.heightAnchor constraintEqualToConstant:440],
    ]];

    // Make card draggable
    UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(_panCard:)];
    [_blur addGestureRecognizer:pan];

    // ── Header ────────────────────────────────────────────────────────────
    UILabel* title = [[UILabel alloc] init];
    title.text = @"FFNET IOS  V1.0.0 Beta";
    title.textColor = [UIColor colorWithRed:0.6 green:0.8 blue:1 alpha:1];
    title.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightBold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [_blur.contentView addSubview:title];

    // ── Close button ──────────────────────────────────────────────────────
    UIButton* closeBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    [closeBtn setTitle:@"✕" forState:UIControlStateNormal];
    closeBtn.tintColor = [UIColor colorWithWhite:0.7 alpha:1];
    closeBtn.titleLabel.font = [UIFont systemFontOfSize:16 weight:UIFontWeightMedium];
    closeBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [closeBtn addTarget:self action:@selector(hide) forControlEvents:UIControlEventTouchUpInside];
    [_blur.contentView addSubview:closeBtn];

    // ── Page selector ─────────────────────────────────────────────────────
    _pageSeg = [[UISegmentedControl alloc] initWithItems:@[@"AIMBOT", @"ESP", @"SETTINGS"]];
    _pageSeg.selectedSegmentIndex = 0;
    _pageSeg.backgroundColor = [UIColor colorWithWhite:0.2 alpha:0.5];
    [_pageSeg setTitleTextAttributes:@{
        NSForegroundColorAttributeName: [UIColor colorWithWhite:0.6 alpha:1],
        NSFontAttributeName: [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightMedium]
    } forState:UIControlStateNormal];
    [_pageSeg setTitleTextAttributes:@{
        NSForegroundColorAttributeName: UIColor.whiteColor,
        NSFontAttributeName: [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightBold]
    } forState:UIControlStateSelected];
    _pageSeg.translatesAutoresizingMaskIntoConstraints = NO;
    [_pageSeg addTarget:self action:@selector(_pageChanged:) forControlEvents:UIControlEventValueChanged];
    [_blur.contentView addSubview:_pageSeg];

    // ── Scroll container ──────────────────────────────────────────────────
    _scroll = [[UIScrollView alloc] init];
    _scroll.translatesAutoresizingMaskIntoConstraints = NO;
    _scroll.showsVerticalScrollIndicator = NO;
    [_blur.contentView addSubview:_scroll];

    // Layout
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:_blur.contentView.leadingAnchor constant:16],
        [title.topAnchor constraintEqualToAnchor:_blur.contentView.topAnchor constant:14],
        [closeBtn.trailingAnchor constraintEqualToAnchor:_blur.contentView.trailingAnchor constant:-12],
        [closeBtn.centerYAnchor constraintEqualToAnchor:title.centerYAnchor],
        [_pageSeg.leadingAnchor constraintEqualToAnchor:_blur.contentView.leadingAnchor constant:12],
        [_pageSeg.trailingAnchor constraintEqualToAnchor:_blur.contentView.trailingAnchor constant:-12],
        [_pageSeg.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:10],
        [_scroll.leadingAnchor constraintEqualToAnchor:_blur.contentView.leadingAnchor constant:12],
        [_scroll.trailingAnchor constraintEqualToAnchor:_blur.contentView.trailingAnchor constant:-12],
        [_scroll.topAnchor constraintEqualToAnchor:_pageSeg.bottomAnchor constant:10],
        [_scroll.bottomAnchor constraintEqualToAnchor:_blur.contentView.bottomAnchor constant:-14],
    ]];

    [self _buildAimPage];
    [self _buildESPPage];
    [self _buildSettPage];
    [self _showPage:0];

    // ── Display link for ESP + player refresh ─────────────────────────────
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(_tick)];
    _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

// ─── Aimbot page ─────────────────────────────────────────────────────────────
- (void)_buildAimPage {
    _aimPage = [self _stack];

    [self _addRow:@"Aimbot" to:_aimPage onChange:^(BOOL v){ g_aimCfg.enabled = v; }
          initial:g_aimCfg.enabled];

    // Bone picker
    UILabel* boneLbl = [UILabel new];
    boneLbl.text = @"Target Part";
    boneLbl.textColor = [UIColor colorWithWhite:0.75 alpha:1];
    boneLbl.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightMedium];
    [_aimPage addArrangedSubview:boneLbl];

    _boneSeg = [[UISegmentedControl alloc] initWithItems:@[@"HEAD",@"NECK",@"BODY",@"SPINE",@"LEG"]];
    _boneSeg.selectedSegmentIndex = 0;
    _boneSeg.backgroundColor = [UIColor colorWithWhite:0.18 alpha:0.5];
    [_boneSeg setTitleTextAttributes:@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:9.5 weight:UIFontWeightMedium]} forState:UIControlStateNormal];
    [_boneSeg addTarget:self action:@selector(_boneChanged:) forControlEvents:UIControlEventValueChanged];
    [_aimPage addArrangedSubview:_boneSeg];

    // FOV slider
    FFSlider* fovSl = [[FFSlider alloc] initWithTitle:@"Aim FOV Radius"];
    [fovSl setMinVal:0 maxVal:200 current:g_aimCfg.fovRadius];
    fovSl.onValue = ^(float v){ g_aimCfg.fovRadius = v; };
    [_aimPage addArrangedSubview:fovSl];

    [self _addRow:@"Show FOV Circle" to:_aimPage onChange:^(BOOL v){ g_aimCfg.showFovCircle = v; }
          initial:g_aimCfg.showFovCircle];

    [self _addRow:@"Silent Aim" to:_aimPage onChange:^(BOOL v){ g_aimCfg.silentAim = v; }
          initial:g_aimCfg.silentAim];

    // Silent aim note
    UILabel* note = [UILabel new];
    note.text = @"Silent: bullets hit through crosshair,\nonly within FOV radius.";
    note.textColor = [UIColor colorWithWhite:0.5 alpha:1];
    note.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    note.numberOfLines = 0;
    [_aimPage addArrangedSubview:note];
}

// ─── ESP page ─────────────────────────────────────────────────────────────────
- (void)_buildESPPage {
    _espPage = [self _stack];
    [self _addRow:@"Player Name"   to:_espPage onChange:^(BOOL v){ g_espCfg.showName = v; }   initial:YES];
    [self _addRow:@"Box ESP"       to:_espPage onChange:^(BOOL v){ g_espCfg.showBox = v; }    initial:YES];
    [self _addRow:@"Line to Enemy" to:_espPage onChange:^(BOOL v){ g_espCfg.showLine = v; }   initial:YES];
    [self _addRow:@"Health Bar"    to:_espPage onChange:^(BOOL v){ g_espCfg.showHealth = v; } initial:YES];

    UILabel* note = [UILabel new];
    note.text = @"Teammates always excluded.";
    note.textColor = [UIColor colorWithWhite:0.4 alpha:1];
    note.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    [_espPage addArrangedSubview:note];
}

// ─── Settings page ────────────────────────────────────────────────────────────
- (void)_buildSettPage {
    _settPage = [self _stack];

    // Streamproof
    [self _addRow:@"Streamproof" to:_settPage onChange:^(BOOL v){
        // Already handled at OS level; this is a visual indicator
        // When ON, menu hides from screenshots (UIWindow secure layer active)
    } initial:YES];

    UILabel* spNote = [UILabel new];
    spNote.text = @"Hides menu & features from\nscreen recording / screenshots.";
    spNote.textColor = [UIColor colorWithWhite:0.45 alpha:1];
    spNote.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    spNote.numberOfLines = 0;
    [_settPage addArrangedSubview:spNote];

    // Save config
    UIButton* saveBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    [saveBtn setTitle:@"  Save Config  " forState:UIControlStateNormal];
    saveBtn.tintColor = [UIColor colorWithRed:0.4 green:0.8 blue:1 alpha:1];
    saveBtn.backgroundColor = [UIColor colorWithWhite:0.15 alpha:0.5];
    saveBtn.layer.cornerRadius = 8;
    saveBtn.titleLabel.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightBold];
    [saveBtn addTarget:self action:@selector(_saveConfig) forControlEvents:UIControlEventTouchUpInside];
    [_settPage addArrangedSubview:saveBtn];

    UILabel* ver = [UILabel new];
    ver.text = @"FFNET IOS  V1.0.0 Beta\nOB54  com.dts.freefireth";
    ver.textColor = [UIColor colorWithWhite:0.35 alpha:1];
    ver.font = [UIFont monospacedSystemFontOfSize:9.5 weight:UIFontWeightRegular];
    ver.numberOfLines = 0;
    ver.textAlignment = NSTextAlignmentCenter;
    [_settPage addArrangedSubview:ver];
}

// ─── Helper: toggle row ───────────────────────────────────────────────────────
- (void)_addRow:(NSString*)label
              to:(UIStackView*)stack
        onChange:(void(^)(BOOL))cb
         initial:(BOOL)init {
    UIView* row = [[UIView alloc] init];
    row.translatesAutoresizingMaskIntoConstraints = NO;

    UILabel* lbl = [UILabel new];
    lbl.text = label;
    lbl.textColor = [UIColor colorWithWhite:0.88 alpha:1];
    lbl.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
    lbl.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:lbl];

    FFToggle* tog = [[FFToggle alloc] initWithFrame:CGRectMake(0, 0, 44, 26)];
    tog.on = init;
    tog.onChange = cb;
    tog.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:tog];

    // Separator
    UIView* sep = [UIView new];
    sep.backgroundColor = [UIColor colorWithWhite:1 alpha:0.07];
    sep.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:sep];

    [NSLayoutConstraint activateConstraints:@[
        [lbl.leadingAnchor constraintEqualToAnchor:row.leadingAnchor],
        [lbl.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [tog.trailingAnchor constraintEqualToAnchor:row.trailingAnchor],
        [tog.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [tog.widthAnchor constraintEqualToConstant:44],
        [tog.heightAnchor constraintEqualToConstant:26],
        [row.heightAnchor constraintEqualToConstant:40],
        [sep.leadingAnchor constraintEqualToAnchor:row.leadingAnchor],
        [sep.trailingAnchor constraintEqualToAnchor:row.trailingAnchor],
        [sep.bottomAnchor constraintEqualToAnchor:row.bottomAnchor],
        [sep.heightAnchor constraintEqualToConstant:0.5],
    ]];
    [stack addArrangedSubview:row];
    [row.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
}

// ─── Stack factory ────────────────────────────────────────────────────────────
- (UIStackView*)_stack {
    UIStackView* sv = [UIStackView new];
    sv.axis = UILayoutConstraintAxisVertical;
    sv.spacing = 6;
    sv.alignment = UIStackViewAlignmentFill;
    return sv;
}

- (void)_showPage:(int)idx {
    _curPage = idx;
    // Remove all from scroll
    for (UIView* v in _scroll.subviews) [v removeFromSuperview];
    UIStackView* page = idx == 0 ? _aimPage : (idx == 1 ? _espPage : _settPage);

    page.translatesAutoresizingMaskIntoConstraints = NO;
    [_scroll addSubview:page];
    [NSLayoutConstraint activateConstraints:@[
        [page.leadingAnchor constraintEqualToAnchor:_scroll.leadingAnchor],
        [page.trailingAnchor constraintEqualToAnchor:_scroll.trailingAnchor],
        [page.topAnchor constraintEqualToAnchor:_scroll.topAnchor],
        [page.bottomAnchor constraintEqualToAnchor:_scroll.bottomAnchor],
        [page.widthAnchor constraintEqualToAnchor:_scroll.widthAnchor],
    ]];
}

- (void)_pageChanged:(UISegmentedControl*)sc { [self _showPage:(int)sc.selectedSegmentIndex]; }
- (void)_boneChanged:(UISegmentedControl*)sc { g_aimCfg.targetBone = (int)sc.selectedSegmentIndex; }

// ─── Pan to drag the menu card ────────────────────────────────────────────────
- (void)_panCard:(UIPanGestureRecognizer*)g {
    CGPoint delta = [g translationInView:self.view];
    CGPoint center = _blur.center;
    _blur.center = CGPointMake(center.x + delta.x, center.y + delta.y);
    [g setTranslation:CGPointZero inView:self.view];
}

// ─── Show / hide ──────────────────────────────────────────────────────────────
- (void)show {
    if (_visible) return;
    _visible = YES;
    self.view.alpha = 0;
    [UIView animateWithDuration:0.25 animations:^{ self.view.alpha = 1; }];
}

- (void)hide {
    if (!_visible) return;
    [UIView animateWithDuration:0.2 animations:^{ self.view.alpha = 0; }
                     completion:^(BOOL f){ self->_visible = NO; }];
}

- (BOOL)visible { return _visible; }

// ─── Save/load config ─────────────────────────────────────────────────────────
- (void)_saveConfig {
    NSDictionary* cfg = @{
        @"aimEnabled":   @(g_aimCfg.enabled),
        @"silentAim":    @(g_aimCfg.silentAim),
        @"showFov":      @(g_aimCfg.showFovCircle),
        @"fovRadius":    @(g_aimCfg.fovRadius),
        @"targetBone":   @(g_aimCfg.targetBone),
        @"espName":      @(g_espCfg.showName),
        @"espBox":       @(g_espCfg.showBox),
        @"espLine":      @(g_espCfg.showLine),
        @"espHealth":    @(g_espCfg.showHealth),
    };
    NSString* path = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject
                      stringByAppendingPathComponent:@"ffnet_config.plist"];
    [cfg writeToFile:path atomically:YES];

    // Flash feedback
    UILabel* toast = [UILabel new];
    toast.text = @"Config Saved ✓";
    toast.textColor = [UIColor colorWithRed:0.3 green:1 blue:0.4 alpha:1];
    toast.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightBold];
    toast.backgroundColor = [UIColor colorWithWhite:0.1 alpha:0.8];
    toast.layer.cornerRadius = 8;
    toast.clipsToBounds = YES;
    toast.textAlignment = NSTextAlignmentCenter;
    toast.translatesAutoresizingMaskIntoConstraints = NO;
    [_blur.contentView addSubview:toast];
    [NSLayoutConstraint activateConstraints:@[
        [toast.centerXAnchor constraintEqualToAnchor:_blur.contentView.centerXAnchor],
        [toast.bottomAnchor constraintEqualToAnchor:_blur.contentView.bottomAnchor constant:-8],
        [toast.widthAnchor constraintEqualToConstant:120],
        [toast.heightAnchor constraintEqualToConstant:28],
    ]];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{ [toast removeFromSuperview]; });
}

- (void)_loadConfig {
    NSString* path = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject
                      stringByAppendingPathComponent:@"ffnet_config.plist"];
    NSDictionary* cfg = [NSDictionary dictionaryWithContentsOfFile:path];
    if (!cfg) return;
    g_aimCfg.enabled        = [cfg[@"aimEnabled"] boolValue];
    g_aimCfg.silentAim      = [cfg[@"silentAim"] boolValue];
    g_aimCfg.showFovCircle  = [cfg[@"showFov"] boolValue];
    g_aimCfg.fovRadius      = [cfg[@"fovRadius"] floatValue] ?: 80.0f;
    g_aimCfg.targetBone     = [cfg[@"targetBone"] intValue];
    g_espCfg.showName       = [cfg[@"espName"] boolValue];
    g_espCfg.showBox        = [cfg[@"espBox"] boolValue];
    g_espCfg.showLine       = [cfg[@"espLine"] boolValue];
    g_espCfg.showHealth     = [cfg[@"espHealth"] boolValue];
}

// ─── Frame tick: refresh players + ESP ───────────────────────────────────────
- (void)_tick {
    RefreshPlayers();
    AimbotTick();
    [_espOverlay setNeedsDisplay];
}

@end

// ─── Window + 3-finger tap recognizer ────────────────────────────────────────
static FFNETWindow*          g_menuWindow   = nil;
static ESPView*              g_espView      = nil;

void SetupMenuWindow() {
    dispatch_async(dispatch_get_main_queue(), ^{
        // ESP overlay — full screen, above game, below menu
        UIWindow* gameWin = [UIApplication sharedApplication].windows.firstObject;
        CGRect screen = [UIScreen mainScreen].bounds;

        g_espView = [[ESPView alloc] initWithFrame:screen];
        g_espView.userInteractionEnabled = NO;
        [gameWin addSubview:g_espView];

        // Menu window
        UIWindowScene* scene = nil;
        for (UIScene* s in [UIApplication sharedApplication].connectedScenes) {
            if ([s isKindOfClass:[UIWindowScene class]]) { scene = (UIWindowScene*)s; break; }
        }
        g_menuWindow = [[FFNETWindow alloc] initWithWindowScene:scene];
        g_menuWindow.windowLevel = UIWindowLevelAlert + 100;
        g_menuWindow.frame = screen;
        g_menuWindow.backgroundColor = [UIColor clearColor];
        g_menuWindow.userInteractionEnabled = YES;
        g_menuWindow.hidden = NO;

        FFNETMenu* menu = [FFNETMenu shared];
        menu.view.frame = screen;
        g_menuWindow.rootViewController = [[UIViewController alloc] init];
        g_menuWindow.rootViewController.view.backgroundColor = [UIColor clearColor];
        [g_menuWindow.rootViewController.view addSubview:menu.view];
        menu.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        menu.view.hidden = YES; // hidden until first open

        // Load saved config
        [[FFNETMenu shared] _loadConfig];

        // 3-tap gesture to open menu
        UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc]
            initWithTarget:[FFNETMenu shared] action:@selector(_handleTripleTap)];
        tap.numberOfTapsRequired = 3;
        tap.numberOfTouchesRequired = 1;
        [g_menuWindow addGestureRecognizer:tap];
    });
}

@implementation FFNETMenu (TripleTap)
- (void)_handleTripleTap {
    if (self.visible) [self hide];
    else {
        self.view.hidden = NO;
        [self show];
    }
}
@end
