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
    FlexItem** items;  // Change from FlexItem* to FlexItem**
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

// Forward declarations
static void initializeItems(FlexContainer* container, FlexItem* layoutItems, int* layoutCount);
static void createFlexLines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, FlexLine** lines, int* lineCount);
static void processFlexLine(FlexContainer* container, FlexLine* line, FlexLine* lines, float mainSize, float crossPos, int isRow, int isReverse);
static void applyFlexAdjustments(FlexLine* line, FlexLine* lines,float freeSpace);
static void positionItemsMainAxis(FlexContainer* container, FlexLine* line, float mainSize, int isRow, int isReverse);
static void positionItemsCrossAxis(FlexContainer* container, FlexLine* line, float crossSize, float crossPos, int isRow);
static void updateOriginalItems(FlexContainer* container, FlexItem* layoutItems, int layoutCount);

// Main layout function
void layoutFlexContainer(FlexContainer* container) {
    printf("\n=== Starting layoutFlexContainer ===\n");
    printf("Container: width=%.1f, height=%.1f, gap=%.1f, items=%d, justify=%d, alignItems=%d\n", 
           container->width, container->height, container->gap, container->itemCount, container->justify, container->alignItems);

    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;
    // Handle zero or negative dimensions
    if (mainSize <= 0) mainSize = 0;
    if (crossSize <= 0) crossSize = 0;

    // Filter and initialize items
    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    initializeItems(container, layoutItems, &layoutCount);

    // Create flex lines
    FlexLine* lines = NULL;
    int lineCount = 0;
    createFlexLines(container, layoutItems, layoutCount, &lines, &lineCount);

    // Process each line
    float crossPos = 0;
    for (int l = 0; l < lineCount; l++) {
        processFlexLine(container, &lines[l], lines, mainSize, crossPos, isRow, isReverse);
        crossPos += lines[l].height + (l < lineCount - 1 ? container->gap : 0);
    }

    // Update original items
    updateOriginalItems(container, layoutItems, layoutCount);

    // Cleanup
    free(layoutItems);
    for (int i = 0; i < lineCount; i++) free(lines[i].items);
    free(lines);
    printf("=== Layout completed ===\n");
}

// Sub-functions
static void initializeItems(FlexContainer* container, FlexItem* layoutItems, int* layoutCount) {
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[*layoutCount] = container->items[i];
            layoutItems[*layoutCount].positionCoords = (Point){0, 0};
            if (layoutItems[*layoutCount].alignSelf == ALIGN_START) {
                layoutItems[*layoutCount].alignSelf = container->alignItems;
            }
            layoutItems[*layoutCount].width = resolveFlexBasis(&layoutItems[*layoutCount]);
            applyConstraints(&layoutItems[*layoutCount]);
            (*layoutCount)++;
        }
    }
    printf("Filtered %d items for layout\n", *layoutCount);
}

static void createFlexLines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, 
                          FlexLine** lines, int* lineCount) {
    float remainingSpace = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                         ? container->width : container->height;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    
    *lineCount = 0;
    *lines = NULL;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? container->gap : 0);
        
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, (currentLine.itemCount + 1) * sizeof(FlexItem*));
            currentLine.items[currentLine.itemCount++] = &layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmax(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        } else {
            if (currentLine.itemCount > 0) {
                *lines = realloc(*lines, (*lineCount + 1) * sizeof(FlexLine));
                (*lines)[(*lineCount)++] = currentLine;
                currentLine = (FlexLine){ .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
            }
            currentLine.items = realloc(currentLine.items, sizeof(FlexItem*));
            currentLine.items[0] = &layoutItems[i];
            currentLine.itemCount = 1;
            currentLine.totalBaseSize = itemSize;
            currentLine.height = layoutItems[i].height;
            remainingSpace = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                           ? container->width - itemSize : container->height - itemSize;
        }
    }
    if (currentLine.itemCount > 0) {
        *lines = realloc(*lines, (*lineCount + 1) * sizeof(FlexLine));
        (*lines)[(*lineCount)++] = currentLine;
    } else {
        free(currentLine.items);  // Clean up if no items were added
    }
    printf("Created %d lines\n", *lineCount);
}

static void processFlexLine(FlexContainer* container, FlexLine* line, FlexLine* lines, float mainSize, float crossPos, 
                          int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    if (mainSize <= 0) {
        freeSpace = 0;
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            // When mainSize is 0, stack items without gaps
            line->totalBaseSize += 0;  // Don't accumulate size
        }
    } else {
        applyFlexAdjustments(line, lines, freeSpace);
        
        // Recalculate total size after flex adjustments
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize += (isRow ? line->items[i]->width : line->items[i]->height) + 
                                 (i > 0 ? container->gap : 0);
        }
        freeSpace = mainSize - line->totalBaseSize;
    }
    
    positionItemsMainAxis(container, line, mainSize, isRow, isReverse);
    positionItemsCrossAxis(container, line, isRow ? container->height : container->width, crossPos, isRow);
}

static void applyFlexAdjustments(FlexLine* line, FlexLine* lines, float freeSpace) {
    float totalGrow = 0, totalShrink = 0;
    for (int i = 0; i < line->itemCount; i++) {
        totalGrow += line->items[i]->flexGrow;
        totalShrink += line->items[i]->flexShrink;
    }
    printf("Line %d: freeSpace=%.1f, totalGrow=%.1f, totalShrink=%.1f\n", 
           line - lines, freeSpace, totalGrow, totalShrink);

    if (freeSpace > 0 && totalGrow > 0) {
        float growPerUnit = freeSpace / totalGrow;
        for (int i = 0; i < line->itemCount; i++) {
            line->items[i]->width += line->items[i]->flexGrow * growPerUnit;
            applyConstraints(line->items[i]);
            printf("Grow item %d: width=%.1f\n", i, line->items[i]->width);
        }
    } else if (freeSpace < 0 && totalShrink > 0) {
        float shrinkPerUnit = fabs(freeSpace) / totalShrink;
        for (int i = 0; i < line->itemCount; i++) {
            line->items[i]->width -= line->items[i]->flexShrink * shrinkPerUnit;
            applyConstraints(line->items[i]);
            printf("Shrink item %d: width=%.1f\n", i, line->items[i]->width);
        }
    }
}

static void positionItemsMainAxis(FlexContainer* container, FlexLine* line, float mainSize, 
                                int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    float mainPos = 0, spacing = 0;
    switch (container->justify) {
        case JUSTIFY_END: mainPos = freeSpace; break;
        case JUSTIFY_CENTER: mainPos = freeSpace / 2; break;
        case JUSTIFY_SPACE_BETWEEN: 
            spacing = (line->itemCount > 1) ? freeSpace / (line->itemCount - 1) : 0; break;
        case JUSTIFY_SPACE_AROUND: 
            spacing = freeSpace / line->itemCount; mainPos = spacing / 2; break;
        case JUSTIFY_SPACE_EVENLY: 
            spacing = freeSpace / (line->itemCount + 1); mainPos = spacing; break;
        default: break;
    }
    printf("Main axis: mainPos=%.1f, spacing=%.1f\n", mainPos, spacing);

    float currentPos;
    if (isReverse) {
        currentPos = mainSize - mainPos;  // Start from right edge adjusted by justification
        for (int i = line->itemCount - 1; i >= 0; i--) {  // Count down from last item
            if (isRow) {
                line->items[i]->positionCoords.x = currentPos - line->items[i]->width;
                currentPos = line->items[i]->positionCoords.x;
            } else {
                line->items[i]->positionCoords.y = currentPos - line->items[i]->height;
                currentPos = line->items[i]->positionCoords.y;
            }
            if (i > 0) {  // Apply gap/spacing before next item
                currentPos -= container->gap;
                if (container->justify == JUSTIFY_SPACE_BETWEEN || 
                    container->justify == JUSTIFY_SPACE_EVENLY ||
                    container->justify == JUSTIFY_SPACE_AROUND) {
                    currentPos -= spacing;
                }
            }
            printf("Item %d: pos=%.1f\n", i, isRow ? line->items[i]->positionCoords.x : line->items[i]->positionCoords.y);
        }
    } else {
        currentPos = mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            int idx = i;
            if (isRow) {
                line->items[idx]->positionCoords.x = currentPos;
            } else {
                line->items[idx]->positionCoords.y = currentPos;
            }
            currentPos += (isRow ? line->items[idx]->width : line->items[idx]->height);
            if (i < line->itemCount - 1) {
                currentPos += container->gap;
                if (container->justify == JUSTIFY_SPACE_BETWEEN || 
                    container->justify == JUSTIFY_SPACE_EVENLY ||
                    container->justify == JUSTIFY_SPACE_AROUND) {
                    currentPos += spacing;
                }
            }
            printf("Item %d: pos=%.1f\n", idx, isRow ? line->items[idx]->positionCoords.x : line->items[idx]->positionCoords.y);
        }
    }
}

static void positionItemsCrossAxis(FlexContainer* container, FlexLine* line, float crossSize, 
                                 float crossPos, int isRow) {
    for (int i = 0; i < line->itemCount; i++) {
        float itemCrossSize = isRow ? line->items[i]->height : line->items[i]->width;
        float itemCrossPos = crossPos;
        
        switch (line->items[i]->alignSelf) {
            case ALIGN_END: itemCrossPos = crossPos + (crossSize - itemCrossSize); break;
            case ALIGN_CENTER: itemCrossPos = crossPos + (crossSize - itemCrossSize) / 2; break;
            case ALIGN_STRETCH: 
                if (isRow) line->items[i]->height = crossSize;
                else line->items[i]->width = crossSize;
                itemCrossPos = crossPos;
                break;
            default: itemCrossPos = crossPos; break;
        }
        
        if (isRow) {
            line->items[i]->positionCoords.y = itemCrossPos;
        } else {
            line->items[i]->positionCoords.x = itemCrossPos;
        }
        printf("Item %d: crossPos=%.1f\n", i, itemCrossPos);
    }
}

static void updateOriginalItems(FlexContainer* container, FlexItem* layoutItems, int layoutCount) {
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k];
            printf("Final item %d: x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
                   i, container->items[i].positionCoords.x, container->items[i].positionCoords.y,
                   container->items[i].width, container->items[i].height);
            k++;
        }
    }
}