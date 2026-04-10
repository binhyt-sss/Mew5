import cv2
import sys

# 21 keypoints từ output của board
keypoints_raw = """
216.562500 t  1544.687500
465.000000 t  1740.000000
772.500000 t  1833.750000
1050.000000 t  1880.625000
1267.500000 t  1911.875000
930.000000 t  1380.625000
1237.500000 t  1318.125000
1440.000000 t  1255.625000
1590.000000 t  1224.375000
836.250000 t  1189.218750
1147.500000 t  1044.687500
1342.500000 t  966.562500
1485.000000 t  900.156250
712.500000 t  1060.312500
960.000000 t  884.531250
1132.500000 t  802.500000
1282.500000 t  740.000000
555.000000 t  982.187500
712.500000 t  786.875000
821.250000 t  675.546875
945.000000 t  595.468750
"""

# Parse keypoints
points = []
for line in keypoints_raw.strip().split("\n"):
    parts = line.split("t")
    x = float(parts[0].strip())
    y = float(parts[1].strip())
    points.append((int(x), int(y)))

# Skeleton connections (MediaPipe hand topology)
connections = [
    (0, 1), (1, 2), (2, 3), (3, 4),        # thumb
    (0, 5), (5, 6), (6, 7), (7, 8),        # index
    (0, 9), (9, 10), (10, 11), (11, 12),   # middle
    (0, 13), (13, 14), (14, 15), (15, 16), # ring
    (0, 17), (17, 18), (18, 19), (19, 20), # pinky
    (5, 9), (9, 13), (13, 17),             # palm
]

image_path = sys.argv[1] if len(sys.argv) > 1 else r"D:\Mew5\z7697165913267_0ee371b344521bf923e1d723c7c6fdd0.jpg"
img = cv2.imread(image_path)

if img is None:
    print(f"Không đọc được ảnh: {image_path}")
    sys.exit(1)

# Vẽ skeleton
for a, b in connections:
    cv2.line(img, points[a], points[b], (0, 255, 0), 3)

# Vẽ keypoints
for i, (x, y) in enumerate(points):
    color = (0, 0, 255) if i == 0 else (255, 0, 0)  # wrist đỏ, còn lại xanh
    cv2.circle(img, (x, y), 10, color, -1)
    cv2.putText(img, str(i), (x + 12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)

# Bounding box từ detection
cv2.rectangle(img, (53, 440), (1709, 2041), (0, 255, 255), 3)
cv2.putText(img, "Hand 90.7%", (53, 430), cv2.FONT_HERSHEY_SIMPLEX, 2, (0, 255, 255), 3)

output_path = "output_hand.jpg"
cv2.imwrite(output_path, img)
print(f"Saved: {output_path}")

# Hiển thị
cv2.imshow("Hand Keypoints", img)
cv2.waitKey(0)
cv2.destroyAllWindows()
