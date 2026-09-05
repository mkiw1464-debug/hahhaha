#pragma once
#include <UIKit/UIKit.h>
#include <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <string>
#include "PlayerList.h"
#include "Aimbot.h"

// ─── ESP Config ───────────────────────────────────────────────────────────────
struct ESPCfg {
    bool showName    = true;
    bool showBox     = true;
    bool showLine    = true;
    bool showHealth  = true;
    // showTeammates always false — filtered in PlayerList
};
ESPCfg g_espCfg;

// ─── ESPView — transparent overlay over the game ─────────────────────────────
@interface ESPView : UIView
@end

@implementation ESPView

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    self.backgroundColor = [UIColor clearColor];
    self.userInteractionEnabled = NO;
    return self;
}

- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) return;

    CGSize screenSize = self.bounds.size;
    Vec2 center = {(float)screenSize.width * 0.5f, (float)screenSize.height * 0.5f};

    // ── Enemy counter on top ────────────────────────────────────────────────
    {
        int cnt = (int)g_players.size();
        NSString* cntStr = [NSString stringWithFormat:@"ENEMIES: %d", cnt];
        NSDictionary* attrs = @{
            NSFontAttributeName: [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightBold],
            NSForegroundColorAttributeName: [UIColor colorWithRed:1 green:0.2 blue:0.2 alpha:1]
        };
        [cntStr drawAtPoint:CGPointMake(10, 10) withAttributes:attrs];
    }

    // ── FOV circle (aimbot) ─────────────────────────────────────────────────
    if (g_aimCfg.enabled && g_aimCfg.showFovCircle) {
        CGContextSetStrokeColorWithColor(ctx,
            [UIColor colorWithRed:1 green:1 blue:1 alpha:0.5].CGColor);
        CGContextSetLineWidth(ctx, 1.0f);
        float r = g_aimCfg.fovRadius;
        CGContextStrokeEllipseInRect(ctx, CGRectMake(center.x - r, center.y - r, r*2, r*2));
    }

    // ── Per-enemy ESP ───────────────────────────────────────────────────────
    for (auto& p : g_players) {
        if (p.curHP <= 0) continue;

        // Project head and feet to screen
        Vec2 headSc{}, feetSc{};
        Vec3 head = p.bones[0]; // HASH_HEAD
        Vec3 feet = p.rootPos;
        feet.y -= 0.9f;         // offset below pelvis to approximate feet

        if (!W2S(head, headSc)) continue;
        if (!W2S(feet, feetSc)) feetSc = headSc; // fallback

        float pxH = fabsf(headSc.y - feetSc.y);
        if (pxH < 6.0f) pxH = 6.0f;
        float pxW = pxH * 0.4f;

        float boxX = headSc.x - pxW * 0.5f;
        float boxY = headSc.y - pxH * 0.05f;

        // Health color: green → yellow → red
        float hpRatio = (p.maxHP > 0) ? (float)p.curHP / (float)p.maxHP : 0.0f;
        UIColor* hpColor = [UIColor colorWithRed:(1.0f - hpRatio)
                                           green:hpRatio
                                            blue:0
                                           alpha:1];

        // ── Box ─────────────────────────────────────────────────────────────
        if (g_espCfg.showBox) {
            CGRect boxRect = CGRectMake(boxX, boxY, pxW, pxH);
            // Corner-only box style (cleaner look)
            float cLen = pxW * 0.25f;
            CGContextSetStrokeColorWithColor(ctx, [UIColor whiteColor].CGColor);
            CGContextSetLineWidth(ctx, 1.5f);
            // Top-left
            CGContextMoveToPoint(ctx, boxRect.origin.x, boxRect.origin.y + cLen);
            CGContextAddLineToPoint(ctx, boxRect.origin.x, boxRect.origin.y);
            CGContextAddLineToPoint(ctx, boxRect.origin.x + cLen, boxRect.origin.y);
            // Top-right
            CGContextMoveToPoint(ctx, CGRectGetMaxX(boxRect) - cLen, boxRect.origin.y);
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(boxRect), boxRect.origin.y);
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(boxRect), boxRect.origin.y + cLen);
            // Bottom-left
            CGContextMoveToPoint(ctx, boxRect.origin.x, CGRectGetMaxY(boxRect) - cLen);
            CGContextAddLineToPoint(ctx, boxRect.origin.x, CGRectGetMaxY(boxRect));
            CGContextAddLineToPoint(ctx, boxRect.origin.x + cLen, CGRectGetMaxY(boxRect));
            // Bottom-right
            CGContextMoveToPoint(ctx, CGRectGetMaxX(boxRect) - cLen, CGRectGetMaxY(boxRect));
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(boxRect), CGRectGetMaxY(boxRect));
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(boxRect), CGRectGetMaxY(boxRect) - cLen);
            CGContextStrokePath(ctx);
        }

        // ── Name ────────────────────────────────────────────────────────────
        if (g_espCfg.showName) {
            NSString* nameStr = [NSString stringWithUTF8String:p.name.c_str()];
            NSDictionary* attrs = @{
                NSFontAttributeName: [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightMedium],
                NSForegroundColorAttributeName: [UIColor whiteColor]
            };
            CGSize sz = [nameStr sizeWithAttributes:attrs];
            [nameStr drawAtPoint:CGPointMake(headSc.x - sz.width * 0.5f,
                                             boxY - sz.height - 2)
                  withAttributes:attrs];
        }

        // ── Health bar (left side of box) ─────────────────────────────────
        if (g_espCfg.showHealth) {
            float barX  = boxX - 6.0f;
            float barY  = boxY;
            float barH  = pxH;
            float fillH = barH * hpRatio;
            // Background
            CGContextSetFillColorWithColor(ctx, [UIColor colorWithWhite:0.1 alpha:0.6].CGColor);
            CGContextFillRect(ctx, CGRectMake(barX, barY, 3, barH));
            // Fill
            CGContextSetFillColorWithColor(ctx, hpColor.CGColor);
            CGContextFillRect(ctx, CGRectMake(barX, barY + (barH - fillH), 3, fillH));
            // HP text
            NSString* hpStr = [NSString stringWithFormat:@"%d", p.curHP];
            NSDictionary* hpAttrs = @{
                NSFontAttributeName: [UIFont monospacedSystemFontOfSize:9 weight:UIFontWeightRegular],
                NSForegroundColorAttributeName: hpColor
            };
            [hpStr drawAtPoint:CGPointMake(barX - 2, barY + barH + 1) withAttributes:hpAttrs];
        }

        // ── Line from bottom-center to player feet ────────────────────────
        if (g_espCfg.showLine) {
            CGContextSetStrokeColorWithColor(ctx,
                [UIColor colorWithRed:0.4 green:0.8 blue:1.0 alpha:0.7].CGColor);
            CGContextSetLineWidth(ctx, 0.8f);
            CGContextMoveToPoint(ctx, center.x, screenSize.height);
            CGContextAddLineToPoint(ctx, feetSc.x, feetSc.y);
            CGContextStrokePath(ctx);
        }
    }
}

@end
