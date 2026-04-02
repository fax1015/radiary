// ============================================================================
// DROP-IN REPLACEMENT for the EffectStackStage::Curves case in DrawEffectsPanel
// (AppWindowUI.cpp)
//
// Replace everything from:
//     case EffectStackStage::Curves: {
// through (inclusive):
//     endEffectCard();
//             break;
//         }
//
// immediately before:
//     case EffectStackStage::Sharpen: {
// ============================================================================

            case EffectStackStage::Curves: {
        const EffectHeaderState curvesHeader = beginEffectCard(
            "##effect_card_curves",
            "Curves",
            scene_.postProcess.curvesEnabled,
            scene_.postProcess.curvesEnabled,
            static_cast<int>(index));
        const Scene beforeCurvesEnabled = scene_;
        if (curvesHeader.enabledChanged) {
            scene_.postProcess.curvesEnabled = curvesHeader.enabled;
        }
        const bool curvesEnabledChanged = curvesHeader.enabledChanged;
        if (curvesHeader.resetPressed) {
            scene_.postProcess.curveBlackPoint = defaultScene.postProcess.curveBlackPoint;
            scene_.postProcess.curveWhitePoint = defaultScene.postProcess.curveWhitePoint;
            scene_.postProcess.curveGamma      = defaultScene.postProcess.curveGamma;
            scene_.postProcess.curveUseCustom  = false;
            scene_.postProcess.curveControlPoints.clear();
        }
        endEffectHeader(curvesHeader.contentOpen, nullptr);
        if (curvesHeader.resetPressed) {
            commitEffectReset(beforeCurvesEnabled, "Curves");
        } else if (curvesEnabledChanged) {
            MarkViewportDirty(DeterminePreviewResetReason(beforeCurvesEnabled, scene_));
            CaptureWidgetUndo(beforeCurvesEnabled, curvesEnabledChanged);
        }
        if (curvesHeader.contentOpen && scene_.postProcess.curvesEnabled) {
            // Always use custom curve mode — the AE-style canvas drives everything.
            scene_.postProcess.curveUseCustom = true;
            auto& cpts = scene_.postProcess.curveControlPoints;
            if (cpts.empty()) {
                cpts = {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
            }

            // Keep points sorted, clamped, and endpoint-anchored every frame.
            for (auto& p : cpts) {
                p.x = std::clamp(p.x, 0.0, 1.0);
                p.y = std::clamp(p.y, 0.0, 1.0);
            }
            std::sort(cpts.begin(), cpts.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x; });
            if (cpts.front().x > 0.001) cpts.insert(cpts.begin(), {0.0, 0.0});
            if (cpts.back().x  < 0.999) cpts.push_back({1.0, 1.0});

            // ── Monotone cubic Hermite interpolation (Fritsch-Carlson) ──────
            // Gives a smooth curve that never overshoots between control points.
            const auto evalCurve = [&](float t) -> float {
                const int n = (int)cpts.size();
                if (n < 2) return t;
                if (t <= (float)cpts.front().x) return (float)cpts.front().y;
                if (t >= (float)cpts.back().x)  return (float)cpts.back().y;

                // Find segment containing t.
                int seg = 0;
                for (int i = 0; i < n - 1; i++) {
                    if (t <= (float)cpts[i + 1].x) { seg = i; break; }
                }

                const float x0 = (float)cpts[seg].x,     y0 = (float)cpts[seg].y;
                const float x1 = (float)cpts[seg + 1].x, y1 = (float)cpts[seg + 1].y;
                const float h  = std::max(1.0e-6f, x1 - x0);
                const float sl = (y1 - y0) / h;   // chord slope

                // One-sided Catmull-Rom tangents at each endpoint.
                float m0 = seg == 0
                    ? sl
                    : 0.5f * (sl + (y0 - (float)cpts[seg - 1].y)
                              / std::max(1.0e-6f, x0 - (float)cpts[seg - 1].x));
                float m1 = seg == n - 2
                    ? sl
                    : 0.5f * (sl + ((float)cpts[seg + 2].y - y1)
                              / std::max(1.0e-6f, (float)cpts[seg + 2].x - x1));

                // Fritsch-Carlson monotonicity constraint.
                if (std::abs(sl) < 1.0e-6f) {
                    m0 = m1 = 0.0f;
                } else {
                    const float alpha = m0 / sl, beta = m1 / sl;
                    const float ab = alpha * alpha + beta * beta;
                    if (ab > 9.0f) {
                        const float tau = 3.0f / std::sqrt(ab);
                        m0 *= tau;
                        m1 *= tau;
                    }
                }

                // Cubic Hermite basis.
                const float u  = (t - x0) / h;
                const float u2 = u * u, u3 = u2 * u;
                return std::clamp(
                    (2.0f*u3 - 3.0f*u2 + 1.0f) * y0 + (u3 - 2.0f*u2 + u) * h * m0 +
                    (-2.0f*u3 + 3.0f*u2)        * y1 + (u3 - u2)          * h * m1,
                    0.0f, 1.0f);
            };

            // ── Canvas geometry ──────────────────────────────────────────────
            const float availW = ImGui::GetContentRegionAvail().x;
            const float cSize  = std::min(availW - 2.0f, 320.0f);
            const ImVec2 cOrigin = ImGui::GetCursorScreenPos();
            const ImRect cv(cOrigin, ImVec2(cOrigin.x + cSize, cOrigin.y + cSize));

            // Curve space [0,1]² ↔ screen coordinate helpers.
            const auto toScr = [&](float x, float y) -> ImVec2 {
                return ImVec2(cv.Min.x + x * cv.GetWidth(),
                              cv.Max.y - y * cv.GetHeight());
            };
            const auto fromScr = [&](ImVec2 s) -> ImVec2 {
                return ImVec2(
                    std::clamp((s.x - cv.Min.x) / cv.GetWidth(),  0.0f, 1.0f),
                    std::clamp((cv.Max.y - s.y) / cv.GetHeight(), 0.0f, 1.0f));
            };

            // Per-widget persistent state (selected point, drag point).
            ImGuiStorage* stor = ImGui::GetStateStorage();
            const ImGuiID sid  = ImGui::GetID("##crv_ae_state");
            int selPt  = stor->GetInt(sid,     -1);
            int dragPt = stor->GetInt(sid + 1, -1);

            ImDrawList* dl    = ImGui::GetWindowDrawList();
            const ImVec2 mouse = ImGui::GetIO().MousePos;

            // ── Background ──────────────────────────────────────────────────
            dl->AddRectFilled(cv.Min, cv.Max,
                ImGui::GetColorU32(theme.panelBackgroundInset), theme.roundingSmall);

            // ── Dashed 4×4 grid ─────────────────────────────────────────────
            {
                const ImU32 gMaj = ImGui::GetColorU32(WithAlpha(theme.borderSubtle, 0.48f));
                const ImU32 gMin = ImGui::GetColorU32(WithAlpha(theme.borderSubtle, 0.20f));
                constexpr float kDash = 4.0f, kGap = 4.0f;
                for (int gi = 1; gi < 4; gi++) {
                    const float gt = (float)gi / 4.0f;
                    const float gx = cv.Min.x + gt * cv.GetWidth();
                    const float gy = cv.Min.y + gt * cv.GetHeight();
                    const ImU32 gc = (gi == 2) ? gMaj : gMin;
                    for (float p = cv.Min.y; p < cv.Max.y; p += kDash + kGap)
                        dl->AddLine(ImVec2(gx, p), ImVec2(gx, std::min(p + kDash, cv.Max.y)), gc);
                    for (float p = cv.Min.x; p < cv.Max.x; p += kDash + kGap)
                        dl->AddLine(ImVec2(p, gy), ImVec2(std::min(p + kDash, cv.Max.x), gy), gc);
                }
            }

            // ── Identity diagonal (amber, After Effects style) ───────────────
            dl->AddLine(
                ImVec2(cv.Min.x, cv.Max.y),   // (0, 0)
                ImVec2(cv.Max.x, cv.Min.y),   // (1, 1)
                ImGui::GetColorU32(ImVec4(0.75f, 0.42f, 0.18f, 0.72f)), 1.4f);

            // ── Sample curve (192 points) ────────────────────────────────────
            constexpr int kSamp = 192;
            std::vector<ImVec2> curveLine;
            curveLine.reserve(kSamp + 1);
            for (int si = 0; si <= kSamp; si++) {
                const float tx = (float)si / (float)kSamp;
                curveLine.push_back(toScr(tx, evalCurve(tx)));
            }

            // Subtle fill under the curve (vertical hairlines).
            {
                const ImU32 fillC = ImGui::GetColorU32(WithAlpha(theme.accent, 0.06f));
                const float barW  = cv.GetWidth() / (float)kSamp + 1.6f;
                for (const auto& cp : curveLine)
                    dl->AddLine(cp, ImVec2(cp.x, cv.Max.y), fillC, barW);
            }

            // Curve line.
            dl->AddPolyline(curveLine.data(), (int)curveLine.size(),
                ImGui::GetColorU32(ImVec4(0.90f, 0.92f, 0.97f, 0.97f)), 0, 2.0f);

            // ── Interaction: invisible button over the canvas ────────────────
            ImGui::SetCursorScreenPos(cOrigin);
            ImGui::InvisibleButton("##crv_canvas", ImVec2(cSize, cSize));
            const bool cvHov = ImGui::IsItemHovered();

            // Find the point under the cursor (7.5 px hit radius).
            int hovPt = -1;
            if (cv.Contains(mouse)) {
                for (int pi = 0; pi < (int)cpts.size(); pi++) {
                    const ImVec2 ps = toScr((float)cpts[pi].x, (float)cpts[pi].y);
                    const float d2  = (mouse.x - ps.x)*(mouse.x - ps.x)
                                    + (mouse.y - ps.y)*(mouse.y - ps.y);
                    if (d2 <= 56.0f) { hovPt = pi; break; }   // 7.5² ≈ 56
                }
            }

            bool cvChanged = false;

            // Double-click → delete interior point.
            bool wasDoubleClick = false;
            if (cvHov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                wasDoubleClick = true;
                if (hovPt > 0 && hovPt < (int)cpts.size() - 1) {
                    PushUndoState(beforeCurvesEnabled, "Remove Curve Point");
                    cpts.erase(cpts.begin() + hovPt);
                    selPt = dragPt = hovPt = -1;
                    cvChanged = true;
                }
            }

            // Single click → start drag on existing point, or add a new one.
            if (!wasDoubleClick && cvHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (hovPt >= 0) {
                    selPt = dragPt = hovPt;
                    PushUndoState(beforeCurvesEnabled, "Edit Curve");
                } else if (cv.Contains(mouse)) {
                    const ImVec2 np = fromScr(mouse);
                    bool tooClose   = false;
                    for (const auto& ep : cpts)
                        if (std::abs((float)ep.x - np.x) < 0.03f) { tooClose = true; break; }
                    if (!tooClose) {
                        PushUndoState(beforeCurvesEnabled, "Add Curve Point");
                        cpts.push_back({(double)np.x, (double)np.y});
                        std::sort(cpts.begin(), cpts.end(),
                            [](const Vec2& a, const Vec2& b) { return a.x < b.x; });
                        // Start dragging the freshly added point.
                        for (int pi = 0; pi < (int)cpts.size(); pi++) {
                            if (std::abs((float)cpts[pi].x - np.x) < 1.0e-4f) {
                                selPt = dragPt = pi; break;
                            }
                        }
                        cvChanged = true;
                    }
                }
            }

            // Drag active point (constrain X between neighbors; endpoints lock to X=0/1).
            if (dragPt >= 0 && dragPt < (int)cpts.size()
                    && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 mp  = fromScr(mouse);
                const bool first = (dragPt == 0), last = (dragPt == (int)cpts.size() - 1);
                const float nx   = first ? 0.0f
                                 : last  ? 1.0f
                                 : std::clamp(mp.x,
                                       (float)cpts[dragPt - 1].x + 0.02f,
                                       (float)cpts[dragPt + 1].x - 0.02f);
                const float ny   = std::clamp(mp.y, 0.0f, 1.0f);
                if ((float)cpts[dragPt].x != nx || (float)cpts[dragPt].y != ny) {
                    cpts[dragPt] = {(double)nx, (double)ny};
                    cvChanged = true;
                }
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragPt = -1;
            }

            stor->SetInt(sid,     selPt);
            stor->SetInt(sid + 1, dragPt);

            // ── Draw square control point handles (After Effects style) ─────
            for (int pi = 0; pi < (int)cpts.size(); pi++) {
                const ImVec2 ps  = toScr((float)cpts[pi].x, (float)cpts[pi].y);
                const bool isDrag = (pi == dragPt);
                const bool isHov  = (pi == hovPt);
                const bool isSel  = (pi == selPt);
                const float half  = isDrag ? 6.0f : (isHov ? 5.5f : 4.5f);
                const ImU32 fill  = isDrag
                    ? ImGui::GetColorU32(theme.accentHover)
                    : isHov ? ImGui::GetColorU32(theme.accent)
                    : isSel ? ImGui::GetColorU32(ImVec4(0.94f, 0.95f, 0.99f, 1.00f))
                            : ImGui::GetColorU32(ImVec4(0.78f, 0.82f, 0.90f, 0.92f));
                dl->AddRectFilled(
                    ImVec2(ps.x - half, ps.y - half), ImVec2(ps.x + half, ps.y + half),
                    fill, 2.0f);
                dl->AddRect(
                    ImVec2(ps.x - half, ps.y - half), ImVec2(ps.x + half, ps.y + half),
                    ImGui::GetColorU32(WithAlpha(theme.panelBackgroundInset, 0.80f)), 2.0f, 0, 1.5f);
            }

            // ── Crosshair + tooltip ──────────────────────────────────────────
            if (cvHov && cv.Contains(mouse)) {
                if (hovPt < 0) {
                    // Show crosshair and curve-hit dot while hovering empty canvas.
                    const float inV  = fromScr(mouse).x;
                    const float outV = evalCurve(inV);
                    const ImVec2 hit = toScr(inV, outV);
                    const ImU32 hc   = ImGui::GetColorU32(WithAlpha(theme.textDim, 0.32f));
                    dl->AddLine(ImVec2(mouse.x, cv.Min.y), ImVec2(mouse.x, cv.Max.y), hc);
                    dl->AddLine(ImVec2(cv.Min.x, hit.y),   ImVec2(cv.Max.x, hit.y),   hc);
                    dl->AddCircleFilled(hit, 3.5f,
                        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.88f)));
                    char tip[64];
                    std::snprintf(tip, sizeof(tip), "In %.2f  →  Out %.2f", inV, outV);
                    ImGui::SetTooltip("%s", tip);
                } else if (hovPt > 0 && hovPt < (int)cpts.size() - 1) {
                    char tip[96];
                    std::snprintf(tip, sizeof(tip),
                        "%.2f, %.2f  ·  Drag to move  ·  Dbl-click to delete",
                        (float)cpts[hovPt].x, (float)cpts[hovPt].y);
                    ImGui::SetTooltip("%s", tip);
                } else {
                    ImGui::SetTooltip("Endpoint — drag vertically to adjust output");
                }
            }

            // ── Canvas border ────────────────────────────────────────────────
            dl->AddRect(cv.Min, cv.Max,
                ImGui::GetColorU32(WithAlpha(theme.borderStrong, 0.55f)),
                theme.roundingSmall, 0, 1.0f);

            // Advance ImGui cursor past the canvas.
            ImGui::SetCursorScreenPos(ImVec2(cOrigin.x, cOrigin.y + cSize + 6.0f));

            // ── Action buttons (Identity | Smooth | S-Curve) ─────────────────
            {
                const float bW      = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
                const Scene beforeBtn = scene_;

                if (DrawActionButton("##crv_id", "Identity", IconGlyph::Remove,
                        ActionTone::Slate, false, true, bW,
                        "Reset to a straight identity (linear) curve")) {
                    PushUndoState(beforeBtn, "Curves: Identity");
                    cpts = {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
                    cvChanged = true;
                }
                ImGui::SameLine(0.0f, 4.0f);

                if (DrawActionButton("##crv_sm", "Smooth", IconGlyph::Settings,
                        ActionTone::Slate, false, (int)cpts.size() > 2, bW,
                        "Laplacian-smooth interior control points")) {
                    PushUndoState(beforeBtn, "Curves: Smooth");
                    for (int iter = 0; iter < 2; iter++) {
                        auto tmp = cpts;
                        for (int pi = 1; pi < (int)cpts.size() - 1; pi++)
                            tmp[pi].y = (cpts[pi-1].y + cpts[pi].y + cpts[pi+1].y) / 3.0;
                        cpts = tmp;
                    }
                    cvChanged = true;
                }
                ImGui::SameLine(0.0f, 4.0f);

                if (DrawActionButton("##crv_sc", "S-Curve", IconGlyph::Randomize,
                        ActionTone::Accent, false, true, bW,
                        "Apply a classic S-curve for contrast boost")) {
                    PushUndoState(beforeBtn, "Curves: S-Curve");
                    cpts = {{0.0, 0.0}, {0.25, 0.18}, {0.5, 0.5}, {0.75, 0.82}, {1.0, 1.0}};
                    cvChanged = true;
                }
            }

            if (cvChanged) {
                MarkViewportDirty(DeterminePreviewResetReason(beforeCurvesEnabled, scene_));
                SyncCurrentKeyframeFromScene();
            }
        }
        endEffectCard();
                break;
            }
