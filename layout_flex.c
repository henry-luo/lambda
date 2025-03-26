#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// enums (unchanged)
typedef enum { DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE } FlexDirection;
typedef enum { WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE } FlexWrap;
typedef enum { JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY } JustifyContent;
typedef enum { 
    ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH,
    ALIGN_SPACE_BETWEEN, ALIGN_SPACE_AROUND, ALIGN_SPACE_EVENLY
} AlignType;
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

// structs (unchanged)
typedef struct { int x, y; } Point;

typedef struct {
    Point pos;
    int width, height;
    int minWidth, maxWidth;
    int minHeight, maxHeight;
    int flexBasis;  // -1 for auto
    float flexGrow, flexShrink;
    int margin[4];
    AlignType alignSelf;
    int order;
    Visibility visibility;
    PositionType position;
    float aspectRatio;
    // Flags for percentage values
    int isWidthPercent : 1;
    int isHeightPercent : 1;
    int isFlexBasisPercent : 1;
    int isMinWidthPercent : 1;
    int isMaxWidthPercent : 1;
    int isMinHeightPercent : 1;
    int isMaxHeightPercent : 1;
} FlexItem;

typedef struct {
    FlexItem** items;
    int itemCount;
    int totalBaseSize;
    int height;
} FlexLine;

typedef struct {
    int width, height;
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType alignItems;
    AlignType alignContent;
    int rowGap;      // Gap between rows
    int columnGap;   // Gap between columns
    FlexItem* items;
    int itemCount;
    WritingMode writingMode;
    TextDirection textDirection;
} FlexContainer;

// Add a temporary structure to keep track of original indices during sorting
typedef struct {
    FlexItem item;
    int originalIndex;
} FlexItemWithIndex;

static void initialize_items(FlexContainer* container, FlexItem* layoutItems, int* layoutCount);
static void create_flex_lines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, FlexLine** lines, int* lineCount);
static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, int lineCount, float mainSize, float crossSize, float* crossPos, int isRow, int isReverse);
static void apply_flex_adjustments(FlexContainer* container, FlexLine* line, FlexLine* lines, float freeSpace);
static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, int isRow, int isReverse);
static void position_items_cross_axis(FlexContainer* container, FlexLine* line, float crossSize, float crossPos, int isRow);
static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount);

// Helper functions
float clamp(float value, float min, float max) {
    float result = (max != 0) ? fmin(fmax(value, min), max) : fmax(value, min);
    printf("clamp(%.1f, %.1f, %.1f) = %.1f\n", value, min, max, result);
    return result;
}

// Modified comparison function for sorting
static int compare_item_order(const void* a, const void* b) {
    FlexItemWithIndex* item_a = (FlexItemWithIndex*)a;
    FlexItemWithIndex* item_b = (FlexItemWithIndex*)b;
    return item_a->item.order - item_b->item.order;
}

// Helper function to resolve percentage values
int resolve_percentage(int value, int isPercent, int containerSize) {
    if (isPercent) {
        // Convert from percentage (0-100) to absolute pixels
        float percentage = (float)value / 100.0f;
        return (int)(percentage * containerSize);
    }
    return value;
}

// Updated function to resolve flex basis with percentage support
int resolve_flex_basis(FlexItem* item, int containerMainSize) {
    int basis;
    if (item->flexBasis == -1) {
        // Auto - use item width (which might be a percentage)
        basis = resolve_percentage(item->width, item->isWidthPercent, containerMainSize);
        basis = basis > 0 ? basis : 0;
    } else {
        // Specific flex-basis value (might be a percentage)
        basis = resolve_percentage(item->flexBasis, item->isFlexBasisPercent, containerMainSize);
        basis = basis > 0 ? basis : resolve_percentage(item->width, item->isWidthPercent, containerMainSize);
    }
    
    printf("resolve_flex_basis: width=%d, flexBasis=%d -> %d\n", 
           item->width, item->flexBasis, basis);
    
    if (basis < 0) basis = 0;
    return basis;
}

// Updated function to apply constraints with percentage support
void apply_constraints(FlexItem* item, int containerWidth, int containerHeight) {
    int oldWidth = item->width, oldHeight = item->height;
    
    // Resolve percentage-based values
    int actualWidth = resolve_percentage(item->width, item->isWidthPercent, containerWidth);
    int actualHeight = resolve_percentage(item->height, item->isHeightPercent, containerHeight);
    int minWidth = resolve_percentage(item->minWidth, item->isMinWidthPercent, containerWidth);
    int maxWidth = resolve_percentage(item->maxWidth, item->isMaxWidthPercent, containerWidth);
    int minHeight = resolve_percentage(item->minHeight, item->isMinHeightPercent, containerHeight);
    int maxHeight = resolve_percentage(item->maxHeight, item->isMaxHeightPercent, containerHeight);
    
    // Adjust dimensions based on aspect ratio
    if (item->aspectRatio > 0) {
        if (actualWidth > 0 && actualHeight == 0) {
            actualHeight = (int)(actualWidth / item->aspectRatio);
        } else if (actualHeight > 0 && actualWidth == 0) {
            actualWidth = (int)(actualHeight * item->aspectRatio);
        }
    }

    // Apply constraints
    actualWidth = clamp(actualWidth, minWidth, maxWidth);
    actualHeight = clamp(actualHeight, minHeight, maxHeight);

    // Reapply aspect ratio constraints after clamping
    if (item->aspectRatio > 0) {
        if (actualWidth > 0 && actualHeight == 0) {
            actualHeight = (int)(actualWidth / item->aspectRatio);
        } else if (actualHeight > 0 && actualWidth == 0) {
            actualWidth = (int)(actualHeight * item->aspectRatio);
        }
    }

    // Update the item with constrained values
    // We preserve the percentage flags but update the values to resolved pixels
    item->width = actualWidth;
    item->height = actualHeight;
    
    printf("apply_constraints: width %d -> %d, height %d -> %d\n", 
           oldWidth, item->width, oldHeight, item->height);
}

// Flex layout sub-functions
static void initialize_items(FlexContainer* container, FlexItem* layoutItems, int* layoutCount) {
    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int containerMainSize = isRow ? container->width : container->height;
    int containerCrossSize = isRow ? container->height : container->width;
    
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[*layoutCount] = container->items[i];
            layoutItems[*layoutCount].pos = (Point){0, 0};
            if (layoutItems[*layoutCount].alignSelf == ALIGN_START) {
                layoutItems[*layoutCount].alignSelf = container->alignItems;
            }
            
            // Resolve flex basis (considering percentages)
            layoutItems[*layoutCount].width = resolve_flex_basis(&layoutItems[*layoutCount], containerMainSize);
            
            // Clear percentage flags since we've resolved to absolute pixels
            layoutItems[*layoutCount].isWidthPercent = 0;
            layoutItems[*layoutCount].isFlexBasisPercent = 0;
            
            // Apply constraints
            apply_constraints(&layoutItems[*layoutCount], container->width, container->height);
            (*layoutCount)++;
        }
    }
    printf("Filtered %d items for layout\n", *layoutCount);
}

static void create_flex_lines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, 
                            FlexLine** lines, int* lineCount) {
    float remainingSpace = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                         ? container->width : container->height;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    
    *lineCount = 0;
    *lines = NULL;

    // Determine which gap to use based on direction
    int mainAxisGap = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                    ? container->columnGap : container->rowGap;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? mainAxisGap : 0);
        
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, (currentLine.itemCount + 1) * sizeof(FlexItem*));
            currentLine.items[currentLine.itemCount++] = &layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmax(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        }
        else {
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
        free(currentLine.items);
    }
    printf("Created %d lines\n", *lineCount);
}

static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, int lineCount,
                            float mainSize, float containerCrossSize, float* crossPos, int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    if (mainSize <= 0) {
        freeSpace = 0;
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize = 0;
        }
    } else {
        apply_flex_adjustments(container, line, lines, freeSpace);
        
        // Use appropriate gap based on direction
        int mainAxisGap = isRow ? container->columnGap : container->rowGap;
        
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize += (isRow ? line->items[i]->width : line->items[i]->height) + 
                                 (i > 0 ? mainAxisGap : 0);
        }
        freeSpace = mainSize - line->totalBaseSize;
    }
    
    position_items_main_axis(container, line, mainSize, isRow, isReverse);
    position_items_cross_axis(container, line, containerCrossSize, *crossPos, isRow);  // Use containerCrossSize
    
    printf("Processed line %ld: crossPos=%.1f, height=%d\n", line - lines, *crossPos, line->height);
}

static void apply_flex_adjustments(FlexContainer* container,FlexLine* line, FlexLine* lines, float freeSpace) {
    float totalGrow = 0, totalShrink = 0;
    for (int i = 0; i < line->itemCount; i++) {
        totalGrow += line->items[i]->flexGrow;
        totalShrink += line->items[i]->flexShrink;
    }
    printf("Line %ld: freeSpace=%.1f, totalGrow=%.1f, totalShrink=%.1f\n", 
           line - lines, freeSpace, totalGrow, totalShrink);

    if (freeSpace > 0 && totalGrow > 0) {
        float remainingSpace = freeSpace;
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexGrow > 0) {
                float growAmount = (remainingSpace * line->items[i]->flexGrow) / totalGrow;
                line->items[i]->width += (int)roundf(growAmount);

                // Adjust height based on aspect ratio
                if (line->items[i]->aspectRatio > 0) {
                    line->items[i]->height = (int)(line->items[i]->width / line->items[i]->aspectRatio);
                }

                apply_constraints(line->items[i], container->width, container->height);
                printf("Grow item %d: width=%d, height=%d\n", i, line->items[i]->width, line->items[i]->height);
            }
        }
    } else if (freeSpace < 0 && totalShrink > 0) {
        float remainingSpace = fabs(freeSpace);
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexShrink > 0) {
                float shrinkAmount = (remainingSpace * line->items[i]->flexShrink) / totalShrink;
                line->items[i]->width -= (int)roundf(shrinkAmount);

                // Adjust height based on aspect ratio
                if (line->items[i]->aspectRatio > 0) {
                    line->items[i]->height = (int)(line->items[i]->width / line->items[i]->aspectRatio);
                }

                apply_constraints(line->items[i], container->width, container->height);
                printf("Shrink item %d: width=%d, height=%d\n", i, line->items[i]->width, line->items[i]->height);
            }
        }
    }
}

static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, 
                                   int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    float mainPos = 0, spacing = 0;
    
    // Select the appropriate gap based on direction
    int mainAxisGap = isRow ? container->columnGap : container->rowGap;
    
    if (mainSize > 0) {
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
    }
    printf("Main axis: mainPos=%.1f, spacing=%.1f\n", mainPos, spacing);

    if (isReverse) {
        float currentPos = mainSize - mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            int idx = isRow ? (line->itemCount - 1 - i) : i; // Use reversed index for row, direct for column
            float itemSize = isRow ? line->items[idx]->width : line->items[idx]->height;
            currentPos -= itemSize;
            if (isRow) {
                line->items[idx]->pos.x = mainSize <= 0 ? 0 : (int)currentPos;
            } else {
                line->items[i]->pos.y = mainSize <= 0 ? 0 : (int)currentPos; // Use i for column_reverse
            }
            if (i < line->itemCount - 1 && mainSize > 0) {
                currentPos -= mainAxisGap + (container->justify >= JUSTIFY_SPACE_BETWEEN ? spacing : 0);
            }
            printf("Item %d: pos=%d\n", idx, isRow ? line->items[idx]->pos.x : line->items[i]->pos.y);
        }
    } else {
        float currentPos = mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            int idx = i;
            if (isRow) {
                line->items[idx]->pos.x = mainSize <= 0 ? 0 : (int)currentPos;
            } else {
                line->items[idx]->pos.y = mainSize <= 0 ? 0 : (int)currentPos;
            }
            if (mainSize > 0) {
                currentPos += (isRow ? line->items[idx]->width : line->items[idx]->height);
                if (i < line->itemCount - 1) {
                    currentPos += mainAxisGap + (container->justify >= JUSTIFY_SPACE_BETWEEN ? spacing : 0);
                }
            }
            printf("Item %d: pos=%d\n", idx, isRow ? line->items[idx]->pos.x : line->items[idx]->pos.y);
        }
    }
}

static void position_items_cross_axis(FlexContainer* container, FlexLine* line, float crossSize, 
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
            line->items[i]->pos.y = itemCrossPos;
        } else {
            line->items[i]->pos.x = itemCrossPos;
        }
        printf("Item %d: crossPos=%.1f\n", i, itemCrossPos);
    }
}

static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount) {
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k];
            printf("Final item %d: x=%d, y=%d, w=%d, h=%d\n",
                   i, container->items[i].pos.x, container->items[i].pos.y,
                   container->items[i].width, container->items[i].height);
            k++;
        }
    }
}

// flex layout main function
void layout_flex_container(FlexContainer* container) {
    printf("\n=== Starting layout_flex_container ===\n");
    printf("Container: width=%d, height=%d, rowGap=%d, columnGap=%d, items=%d, justify=%d, alignItems=%d, alignContent=%d\n", 
           container->width, container->height, container->rowGap, container->columnGap, container->itemCount, container->justify, 
           container->alignItems, container->alignContent);

    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;
    if (mainSize <= 0) mainSize = 0;
    if (crossSize <= 0) crossSize = 0;

    // Use the modified structure to track original indices
    FlexItemWithIndex* itemsWithIndices = malloc(container->itemCount * sizeof(FlexItemWithIndex));
    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    
    // Initialize items and track original indices
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            itemsWithIndices[layoutCount].item = container->items[i];
            itemsWithIndices[layoutCount].item.pos = (Point){0, 0};
            itemsWithIndices[layoutCount].originalIndex = i;
            
            if (itemsWithIndices[layoutCount].item.alignSelf == ALIGN_START) {
                itemsWithIndices[layoutCount].item.alignSelf = container->alignItems;
            }
            
            // Resolve flex basis (considering percentages)
            int mainSize = isRow ? container->width : container->height;
            itemsWithIndices[layoutCount].item.width = 
                resolve_flex_basis(&itemsWithIndices[layoutCount].item, mainSize);
            
            // Clear percentage flags since we've resolved to absolute pixels
            itemsWithIndices[layoutCount].item.isWidthPercent = 0;
            itemsWithIndices[layoutCount].item.isFlexBasisPercent = 0;
            
            // Apply constraints
            apply_constraints(&itemsWithIndices[layoutCount].item, container->width, container->height);
            layoutCount++;
        }
    }
    
    // Sort the items by their order property
    if (layoutCount > 0) {
        qsort(itemsWithIndices, layoutCount, sizeof(FlexItemWithIndex), compare_item_order);
        printf("Items sorted by order property\n");
        
        // Extract just the items for layout processing
        for (int i = 0; i < layoutCount; i++) {
            layoutItems[i] = itemsWithIndices[i].item;
        }
    }

    FlexLine* lines = NULL;
    int lineCount = 0;
    create_flex_lines(container, layoutItems, layoutCount, &lines, &lineCount);

    float totalCrossSize = 0;
    for (int l = 0; l < lineCount; l++) {
        totalCrossSize += lines[l].height;
        if (l < lineCount - 1) totalCrossSize += container->rowGap;
    }
    float freeCrossSpace = crossSize - totalCrossSize;
    float crossPos = 0, crossSpacing = 0;

    if (container->wrap != WRAP_NOWRAP && lineCount > 1 && crossSize > 0) {
        switch (container->alignContent) {
            case ALIGN_END: crossPos = freeCrossSpace; break;
            case ALIGN_CENTER: crossPos = freeCrossSpace / 2; break;
            case ALIGN_SPACE_BETWEEN: crossSpacing = freeCrossSpace / (lineCount - 1); break;
            case ALIGN_SPACE_AROUND: 
                crossSpacing = freeCrossSpace / lineCount; 
                crossPos = crossSpacing / 2; 
                break;
            case ALIGN_SPACE_EVENLY: 
                crossSpacing = freeCrossSpace / (lineCount + 1); 
                crossPos = crossSpacing; 
                break;
            case ALIGN_STRETCH: 
                float stretchFactor = crossSize / totalCrossSize;
                for (int l = 0; l < lineCount; l++) {
                    lines[l].height = (int)(lines[l].height * stretchFactor);
                    // Apply stretched height to each item in the line
                    for (int i = 0; i < lines[l].itemCount; i++) {
                        if (isRow) {
                            lines[l].items[i]->height = lines[l].height;
                        } else {
                            lines[l].items[i]->width = lines[l].height;
                        }
                    }
                }
                totalCrossSize = crossSize;
                freeCrossSpace = 0;
                break;
            default: break;
        }
    }
    printf("Align-content: freeCrossSpace=%.1f, crossPos=%.1f, crossSpacing=%.1f\n", 
           freeCrossSpace, crossPos, crossSpacing);

    if (container->wrap == WRAP_WRAP_REVERSE && isRow) {
        float currentCrossPos = crossSize - crossPos;
        for (int l = 0; l < lineCount; l++) {
            currentCrossPos -= lines[l].height;
            process_flex_line(container, &lines[l], lines, lineCount, mainSize, crossSize, 
                            &currentCrossPos, isRow, isReverse);
            if (l < lineCount - 1) {
                currentCrossPos -= container->rowGap + (container->alignContent >= ALIGN_SPACE_BETWEEN ? crossSpacing : 0);
            }
        }
    } else {
        float currentCrossPos = crossPos;
        for (int l = 0; l < lineCount; l++) {
            process_flex_line(container, &lines[l], lines, lineCount, mainSize, crossSize, 
                            &currentCrossPos, isRow, isReverse);
            if (l < lineCount - 1) {
                currentCrossPos += lines[l].height + container->rowGap + 
                                 (container->alignContent >= ALIGN_SPACE_BETWEEN ? crossSpacing : 0);
            }
        }
    }

    // Modify update_original_items to use the original indices
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            // Find the original index for this layout item
            container->items[itemsWithIndices[k].originalIndex] = layoutItems[k];
            printf("Final item %d: x=%d, y=%d, w=%d, h=%d\n",
                   itemsWithIndices[k].originalIndex, 
                   layoutItems[k].pos.x, 
                   layoutItems[k].pos.y,
                   layoutItems[k].width, 
                   layoutItems[k].height);
            k++;
        }
    }

    free(itemsWithIndices);
    free(layoutItems);
    for (int i = 0; i < lineCount; i++) free(lines[i].items);
    free(lines);
    printf("=== Layout completed ===\n");
}

void free_flex_container(FlexContainer* container) {
    if (container->items) free(container->items);
}