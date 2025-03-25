#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// Enums
typedef enum { DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE } FlexDirection;
typedef enum { WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE } FlexWrap;
typedef enum { JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY } JustifyContent;
typedef enum { ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH } AlignType;
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;

// Structs
typedef struct { float x, y; } Point;
typedef struct {
    float width, height;
    float minWidth, maxWidth;
    float minHeight, maxHeight;
    float flexGrow, flexShrink;
    float flexBasis;
    float margin[4];
    AlignType alignSelf;
    int order;
    Visibility visibility;
    PositionType position;
    float aspectRatio;
    Point positionCoords;
} FlexItem;

typedef struct {
    FlexItem* items;
    int itemCount;
    float totalBaseSize;
    float height;
} FlexLine;

typedef struct {
    float width, height;
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType alignItems;
    AlignType alignContent;
    float gap;
    FlexItem* items;
    int itemCount;
    char* writingMode;
    char* textDirection;
} FlexContainer;

// Helpers
float clamp(float value, float min, float max) {
    float result = (max != 0) ? fmin(fmax(value, min), max) : fmax(value, min);
    printf("clamp(%.1f, %.1f, %.1f) = %.1f\n", value, min, max, result);
    return result;
}

float resolveFlexBasis(FlexItem* item) {
    float basis = (item->flexBasis > 0) ? item->flexBasis : item->width;
    printf("resolveFlexBasis: width=%.1f, flexBasis=%.1f -> %.1f\n", item->width, item->flexBasis, basis);
    assert(basis >= 0 && "Flex basis must be non-negative");
    return basis;
}

void applyConstraints(FlexItem* item) {
    float oldWidth = item->width, oldHeight = item->height;
    item->width = clamp(item->width, item->minWidth, item->maxWidth);
    item->height = clamp(item->height, item->minHeight, item->maxHeight);
    printf("applyConstraints: width %.1f -> %.1f, height %.1f -> %.1f\n", oldWidth, item->width, oldHeight, item->height);
}

void layoutFlexContainer(FlexContainer* container) {
    printf("\n=== Starting layoutFlexContainer ===\n");
    printf("Container: width=%.1f, height=%.1f, gap=%.1f, items=%d, justify=%d, alignItems=%d\n", 
           container->width, container->height, container->gap, container->itemCount, container->justify, container->alignItems);

    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;
    assert(mainSize > 0 && crossSize > 0 && "Container dimensions must be positive");

    // Filter items
    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[layoutCount] = container->items[i];
            layoutItems[layoutCount].positionCoords = (Point){0, 0};
            layoutCount++;
        }
    }

    // Resolve base sizes
    for (int i = 0; i < layoutCount; i++) {
        layoutItems[i].width = resolveFlexBasis(&layoutItems[i]);
        applyConstraints(&layoutItems[i]);
    }

    // Line breaking
    FlexLine* lines = NULL;
    int lineCount = 0;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    float remainingSpace = mainSize;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? container->gap : 0);

        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, (currentLine.itemCount + 1) * sizeof(FlexItem));
            currentLine.items[currentLine.itemCount++] = layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmax(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        } else {
            if (currentLine.itemCount > 0) {
                lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
                lines[lineCount++] = currentLine;
            }
            currentLine = (FlexLine){ .items = malloc(sizeof(FlexItem)), .itemCount = 1, 
                                   .totalBaseSize = itemSize, .height = layoutItems[i].height };
            currentLine.items[0] = layoutItems[i];
            remainingSpace = mainSize - itemSize;
        }
    }
    if (currentLine.itemCount > 0) {
        lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
        lines[lineCount++] = currentLine;
    }

    // Process lines
    float crossPos = 0;
    for (int l = 0; l < lineCount; l++) {
        FlexLine* line = &lines[l];
        float freeSpace = mainSize - line->totalBaseSize;
        float totalGrow = 0, totalShrink = 0;

        // Calculate flex factors
        for (int i = 0; i < line->itemCount; i++) {
            totalGrow += line->items[i].flexGrow;
            totalShrink += line->items[i].flexShrink;
        }

        // Apply flex adjustments
        if (freeSpace > 0 && totalGrow > 0) {
            float growPerUnit = freeSpace / totalGrow;
            for (int i = 0; i < line->itemCount; i++) {
                line->items[i].width += line->items[i].flexGrow * growPerUnit;
                applyConstraints(&line->items[i]);
            }
        } else if (freeSpace < 0 && totalShrink > 0) {
            float shrinkPerUnit = fabs(freeSpace) / totalShrink;
            for (int i = 0; i < line->itemCount; i++) {
                line->items[i].width -= line->items[i].flexShrink * shrinkPerUnit;
                applyConstraints(&line->items[i]);
            }
        }

        // Recalculate line size after flex adjustments
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize += line->items[i].width + (i > 0 ? container->gap : 0);
        }
        freeSpace = mainSize - line->totalBaseSize;

        // Main axis positioning
        float mainPos = 0, spacing = 0;
        switch (container->justify) {
            case JUSTIFY_END: mainPos = freeSpace; break;
            case JUSTIFY_CENTER: mainPos = freeSpace / 2; break;
            case JUSTIFY_SPACE_BETWEEN: 
                spacing = (line->itemCount > 1) ? freeSpace / (line->itemCount - 1) : 0; break;
            case JUSTIFY_SPACE_AROUND: 
                spacing = freeSpace / line->itemCount; 
                mainPos = spacing / 2; 
                break;
            case JUSTIFY_SPACE_EVENLY: 
                spacing = freeSpace / (line->itemCount + 1); 
                mainPos = spacing; 
                break;
            default: break;
        }

        for (int i = 0; i < line->itemCount; i++) {
            int idx = isReverse ? line->itemCount - 1 - i : i;
            if (isRow) {
                line->items[idx].positionCoords.x = mainPos;
            } else {
                line->items[idx].positionCoords.y = mainPos;
            }
            mainPos += line->items[idx].width + container->gap + spacing;
        }

        // Cross axis positioning
        for (int i = 0; i < line->itemCount; i++) {
            AlignType align = (line->items[i].alignSelf == ALIGN_START && container->alignItems != ALIGN_START) 
                            ? container->alignItems 
                            : line->items[i].alignSelf;
            float itemCrossSize = isRow ? line->items[i].height : line->items[i].width;
            float itemCrossPos = crossPos;

            switch (align) {
                case ALIGN_END: 
                    itemCrossPos = crossPos + (crossSize - itemCrossSize); 
                    break;
                case ALIGN_CENTER: 
                    itemCrossPos = crossPos + (crossSize - itemCrossSize) / 2; 
                    break;
                case ALIGN_STRETCH: 
                    if (isRow) line->items[i].height = crossSize;
                    else line->items[i].width = crossSize;
                    break;
                default: break;
            }

            if (isRow) {
                line->items[i].positionCoords.y = itemCrossPos;
            } else {
                line->items[i].positionCoords.x = itemCrossPos;
            }
        }
        crossPos += line->height + (l < lineCount - 1 ? container->gap : 0);
    }

    // Update original items
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k];
            k++;
        }
    }

    // Cleanup
    free(layoutItems);
    for (int i = 0; i < lineCount; i++) free(lines[i].items);
    free(lines);
    printf("=== Layout completed ===\n");
}