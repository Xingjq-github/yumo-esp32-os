from card import make,table,section,API
SOURCE='https://algs4.cs.princeton.edu/home/'
def cards():
    out=[]
    def sort(slug,title,summary,method,steps,caution):
        name=''.join(x.capitalize() for x in slug.split('-'))+'Demo'
        code='import java.util.Arrays;\npublic class '+name+' {\n'+method.strip('\n')+'\n    public static void main(String[] args) {\n        int[] a = {5, 1, 4, 2, 2};\n        sort(a);\n        System.out.println(Arrays.toString(a));\n    }\n}\n'
        out.append(make('algorithm.'+slug,title,'算法','排序',summary,code,caution,SOURCE,'Java 21；复杂度针对所附实现，n为元素数',complete=True,related=['algorithm.complexity','algorithm.stability'],extra=[section('steps','步骤',steps),section('boundaries','边界与预期','主程序输出：[1, 2, 2, 4, 5]\n还需覆盖：空数组、单元素、重复值、有序、逆序。数组参数约定非null；有范围限制的算法见注意事项。')]))
    sort('bubble','冒泡排序','相邻逆序就交换，每轮最大值移到尾部。稳定、原地；最坏和平均O(n^2)，已排序带提前退出时O(n)。',r'''
    static void sort(int[] a) {
        for (int end = a.length - 1; end > 0; end--) {
            boolean changed = false;
            for (int j = 0; j < end; j++) {
                if (a[j] > a[j + 1]) {
                    int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t;
                    changed = true;
                }
            }
            if (!changed) break;
        }
    }
''','[5,1,4,2,2]\n第一轮：[1,4,2,2,5]\n第二轮：[1,2,2,4,5]\n第三轮无交换，结束。','额外空间O(1)。只有严格大于才交换才能保留相等项相对顺序；大数据效率较低。')
    sort('selection','选择排序','每轮选出剩余最小值交换到前端。时间O(n^2)，额外空间O(1)，所附交换实现不稳定。',r'''
    static void sort(int[] a) {
        for (int i = 0; i < a.length - 1; i++) {
            int min = i;
            for (int j = i + 1; j < a.length; j++) if (a[j] < a[min]) min = j;
            int t = a[i]; a[i] = a[min]; a[min] = t;
        }
    }
''','[5,1,4,2,2]\n选1换到首位：[1,5,4,2,2]\n选2：[1,2,4,5,2]\n再选2：[1,2,2,5,4]\n最终：[1,2,2,4,5]','即使输入有序仍进行二次方比较。跨位置交换可能颠倒相等元素顺序。')
    sort('insertion','插入排序','把当前元素插入前面已排序区间。稳定、原地；最坏O(n^2)，近乎有序时接近O(n)。',r'''
    static void sort(int[] a) {
        for (int i = 1; i < a.length; i++) {
            int value = a[i], j = i - 1;
            while (j >= 0 && a[j] > value) { a[j + 1] = a[j]; j--; }
            a[j + 1] = value;
        }
    }
''','[5,1,4,2,2]\n插1：[1,5,4,2,2]\n插4：[1,4,5,2,2]\n插2：[1,2,4,5,2]\n再插2：[1,2,2,4,5]','额外空间O(1)。移动条件采用严格大于，防止相等项跨越。适合小数组或基本有序输入。')
    sort('shell','希尔排序','对间隔为gap的子序列做插入排序，逐步缩小间隔。此实现间隔折半，不稳定，最坏O(n^2)，空间O(1)。',r'''
    static void sort(int[] a) {
        for (int gap = a.length / 2; gap > 0; gap /= 2) {
            for (int i = gap; i < a.length; i++) {
                int v = a[i], j = i;
                while (j >= gap && a[j - gap] > v) { a[j] = a[j - gap]; j -= gap; }
                a[j] = v;
            }
        }
    }
''','[5,1,4,2,2]\ngap=2，各跨步子序列插入排序，得到[2,1,4,2,5]\ngap=1，执行普通插入排序，得到[1,2,2,4,5]','复杂度依赖增量序列，不能把所有希尔排序概括为同一平均复杂度。')
    sort('merge','归并排序','递归拆半并合并有序区间。稳定，时间O(n log n)，辅助数组O(n)，递归栈O(log n)。',r'''
    static void sort(int[] a) { mergeSort(a, new int[a.length], 0, a.length); }
    static void mergeSort(int[] a, int[] tmp, int lo, int hi) {
        if (hi - lo < 2) return;
        int mid = lo + (hi - lo) / 2;
        mergeSort(a, tmp, lo, mid); mergeSort(a, tmp, mid, hi);
        int i = lo, j = mid, k = lo;
        while (i < mid && j < hi) tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
        while (i < mid) tmp[k++] = a[i++];
        while (j < hi) tmp[k++] = a[j++];
        System.arraycopy(tmp, lo, a, lo, hi - lo);
    }
''','[5,1]与[4,2,2]分别排序为[1,5]和[2,2,4]。\n合并依次取较小值：1、2、2、4、5。相等时取左侧，保持稳定。','区间采用左闭右开。不是原地排序；大数组需有辅助数组可用内存。')
    sort('quick','快速排序（三路）','按枢轴分成小于、等于、大于三区，重复值较多时有效。平均O(n log n)，最坏O(n^2)，不稳定。只递归较小区间控制栈深。',r'''
    static void sort(int[] a) { quick(a, 0, a.length - 1); }
    static void swap(int[] a, int x, int y) { int t = a[x]; a[x] = a[y]; a[y] = t; }
    static void quick(int[] a, int lo, int hi) {
        while (lo < hi) {
            int pivot = a[lo + (hi - lo) / 2];
            int lt = lo, i = lo, gt = hi;
            while (i <= gt) {
                if (a[i] < pivot) swap(a, lt++, i++);
                else if (a[i] > pivot) swap(a, i, gt--);
                else i++;
            }
            if (lt - lo < hi - gt) { quick(a, lo, lt - 1); lo = gt + 1; }
            else { quick(a, gt + 1, hi); hi = lt - 1; }
        }
    }
''','[5,1,4,2,2]选中间值4为枢轴。\n分区后小于4的三个元素与等于4、大于4分开。\n继续处理小于区，最终[1,2,2,4,5]。','额外栈空间O(log n)，但时间仍可能退化。末端换来的未知元素需要再次检查，所以大于分支不递增i。')
    sort('heap','堆排序','先建立最大堆，再把堆顶换到尾部并缩小堆。时间O(n log n)，额外空间O(1)，不稳定。',r'''
    static void sort(int[] a) {
        for (int i = a.length / 2 - 1; i >= 0; i--) down(a, i, a.length);
        for (int end = a.length - 1; end > 0; end--) {
            int t = a[0]; a[0] = a[end]; a[end] = t;
            down(a, 0, end);
        }
    }
    static void down(int[] a, int root, int n) {
        while (root < n / 2) {
            int child = root * 2 + 1;
            if (child + 1 < n && a[child + 1] > a[child]) child++;
            if (a[root] >= a[child]) break;
            int t = a[root]; a[root] = a[child]; a[child] = t;
            root = child;
        }
    }
''','[5,1,4,2,2]建堆后最大值5在根。\n把5换到最后，余下范围重新下沉。\n依次固定4、2、2，得到[1,2,2,4,5]。','建堆为O(n)，不是逐个插入的O(n log n)。下沉只比较当前堆边界内的节点。')
    sort('counting','计数排序','按值计数并用前缀和确定稳定输出位置。时间O(n+k)，空间O(n+k)，k为最大值加1。此实现只接受0到1000000。',r'''
    static void sort(int[] a) {
        int max = 0;
        for (int x : a) {
            if (x < 0 || x > 1_000_000) throw new IllegalArgumentException("range");
            max = Math.max(max, x);
        }
        int[] count = new int[max + 1], out = new int[a.length];
        for (int x : a) count[x]++;
        for (int i = 1; i < count.length; i++) count[i] += count[i - 1];
        for (int i = a.length - 1; i >= 0; i--) out[--count[a[i]]] = a[i];
        System.arraycopy(out, 0, a, 0, a.length);
    }
''','[5,1,4,2,2]计数：1出现1次，2出现2次，4和5各1次。\n前缀和给出各值结束位置，倒序放入输出，得到[1,2,2,4,5]。','值域很大而元素很少时不适用。倒序填充是保持稳定性的关键，范围保护防止分配过大计数数组。')
    sort('bucket','桶排序','把0到99的值分到10个桶，各桶做稳定插入排序再连接。平均情况取决于分布；最坏O(n^2)，空间O(n+10)。',r'''
    static void sort(int[] a) {
        var buckets = new java.util.ArrayList<java.util.ArrayList<Integer>>();
        for (int i = 0; i < 10; i++) buckets.add(new java.util.ArrayList<>());
        for (int x : a) {
            if (x < 0 || x >= 100) throw new IllegalArgumentException("range");
            var b = buckets.get(x / 10);
            int p = b.size(); b.add(x);
            while (p > 0 && b.get(p - 1) > x) { b.set(p, b.get(p - 1)); p--; }
            b.set(p, x);
        }
        int i = 0;
        for (var b : buckets) for (int x : b) a[i++] = x;
    }
''','本例[5,1,4,2,2]都落入0到9的桶，因此退化为一次插入排序。\n若输入[21,5,43,12]，依次分入桶2、0、4、1，再按桶连接。','本实现固定10桶，不能宣称任意n下平均线性；装箱和多个列表有额外开销。')
    sort('radix','基数排序（LSD）','从低位到高位执行稳定计数分配。此实现处理非负int，十进制d位时O(d(n+10))，空间O(n+10)，稳定。',r'''
    static void sort(int[] a) {
        int max = 0;
        for (int x : a) {
            if (x < 0) throw new IllegalArgumentException("nonnegative only");
            max = Math.max(max, x);
        }
        int[] out = new int[a.length];
        for (long exp = 1; exp <= max; exp *= 10) {
            int[] count = new int[10];
            for (int x : a) count[(int)(x / exp % 10)]++;
            for (int i = 1; i < 10; i++) count[i] += count[i - 1];
            for (int i = a.length - 1; i >= 0; i--) {
                int digit = (int)(a[i] / exp % 10);
                out[--count[digit]] = a[i];
            }
            System.arraycopy(out, 0, a, 0, a.length);
        }
    }
''','[5,1,4,2,2]仅有个位，一轮后即[1,2,2,4,5]。\n[21,12,13]先按个位为[21,12,13]，再按十位为[12,13,21]。','exp使用long避免int最高位处理时溢出。负数需另行设计符号处理，不能直接取绝对值套用。')
    out+=table(r'''
complexity ~~ 时间与空间复杂度 ~~ O记号描述规模增大时的渐近上界，不是实际毫秒数。必须区分最坏、平均、摊还及输入约束。 ~~ // Nested independent loops: O(n*n)\nfor (int i = 0; i < n; i++)\n    for (int j = 0; j < n; j++) work(i, j); ~~ 示例需提供n和work上下文。递归栈计入额外空间，常数对小设备仍很重要。
stability ~~ 排序稳定性 ~~ 稳定排序保持相等键元素的原始相对顺序，适合分阶段排序和保持先到次序。 ~~ record Item(int key, String id) {}\nvar a = new Item[]{new Item(1,"A"),new Item(1,"B")};\njava.util.Arrays.sort(a, java.util.Comparator.comparingInt(Item::key)); ~~ 原始类型数组无法直接观察相等元素身份，判断稳定性必须检查实现或带标记元素。
binary-search ~~ 二分查找边界 ~~ 在有序序列上每次排除一半，时间O(log n)。所附方法查找首个不小于target的位置，可能返回length。 ~~ static int lowerBound(int[] a, int target) {\n    int lo=0, hi=a.length;\n    while (lo<hi) {\n        int mid=lo+(hi-lo)/2;\n        if (a[mid]<target) lo=mid+1; else hi=mid;\n    }\n    return lo;\n} ~~ 使用左闭右开区间，避免mid+1和边界混乱。返回位置后仍需检查是否相等。
array ~~ 数组与连续存储 ~~ 数组按索引访问，适合密集数据和可预估容量。插入中部通常需要移动后续元素。 ~~ int[] values = new int[8];\nvalues[3] = 42; ~~ Java对象数组存的是引用，目标对象未必连续；容量与逻辑长度分开管理。
linked-list ~~ 链表结构 ~~ 链表节点通过引用连接，适合插删已定位节点。索引定位通常O(n)，额外引用与对象分配有成本。 ~~ static class Node {\n    int value; Node next;\n    Node(int v, Node n) { value=v; next=n; }\n} ~~ 处理空表、头尾修改和循环引用，不能把定位耗时忽略后声称所有插删O(1)。
stack ~~ 栈与后进先出 ~~ 栈适合括号匹配、撤销和深度优先遍历。使用ArrayDeque避免旧Stack的同步与继承负担。 ~~ java.util.Deque<Integer> stack = new java.util.ArrayDeque<>();\nstack.push(1);\nint top = stack.pop(); ~~ 空栈pop抛异常；递归也是隐式使用栈，需要控制深度。
queue ~~ 队列与先进先出 ~~ 队列适合任务排队与广度优先遍历。首尾操作通常可做到摊还O(1)。 ~~ java.util.Deque<Integer> q = new java.util.ArrayDeque<>();\nq.addLast(1);\nInteger first = q.pollFirst(); ~~ 队列需要容量或流控，生产速率长期超过消费速率会耗尽内存。
hash ~~ 哈希表冲突 ~~ 哈希把键映射到桶，冲突通过链、树或开放寻址处理。好分布下平均查找O(1)。 ~~ var seen = new java.util.HashSet<Integer>();\nboolean first = seen.add(42); ~~ hash相同不代表键相等；最坏性能和扩容策略由具体实现决定。
tree ~~ 树与遍历顺序 ~~ 前序先根，中序左根右，后序先子后根；层序使用队列。平衡树高度通常O(log n)。 ~~ static void inorder(Node n) {\n    if (n == null) return;\n    inorder(n.left); visit(n); inorder(n.right);\n} ~~ 示例需提供二叉Node与visit。普通二叉搜索树可能退化为链，遍历深度达O(n)。
heap-structure ~~ 二叉堆性质 ~~ 最大堆父节点不小于子节点，最小堆相反。堆只保证局部顺序，不是全局排序数组。 ~~ var minHeap = new java.util.PriorityQueue<Integer>();\nminHeap.add(3); minHeap.add(1);\nint min = minHeap.remove(); ~~ 堆查任意元素通常仍为O(n)；只需前k项时可维护有界堆。
bfs ~~ 广度优先搜索 ~~ 队列逐层扩展，访问标记在入队时设置，防止同一节点重复入队。无权图可得到最少边数路径。 ~~ // g is an adjacency list; start is valid.\nvar q = new java.util.ArrayDeque<Integer>();\nboolean[] seen = new boolean[g.size()];\nseen[start]=true; q.add(start);\nwhile (!q.isEmpty()) {\n    int v=q.remove();\n    for (int w : g.get(v)) if (!seen[w]) { seen[w]=true; q.add(w); }\n} ~~ 片段需邻接表g和start。加权最短路一般不能直接用普通BFS。
dfs ~~ 深度优先搜索 ~~ 显式栈或递归优先沿路径深入，适合连通性和回溯。图中须记录访问，树中也要防错误循环。 ~~ // g is an adjacency list; start is valid.\nvar stack = new java.util.ArrayDeque<Integer>();\nboolean[] seen = new boolean[g.size()];\nstack.push(start);\nwhile (!stack.isEmpty()) {\n    int v=stack.pop(); if (seen[v]) continue; seen[v]=true;\n    for (int w : g.get(v)) if (!seen[w]) stack.push(w);\n} ~~ 此实现可能重复压栈但不会重复处理；深图使用显式栈避免Java调用栈溢出。
two-pointers ~~ 双指针与滑动窗口 ~~ 利用有序或窗口可增减性质减少重复扫描。移动指针必须保持不变量，不能仅因为有两个索引就宣称线性。 ~~ // Sorted array; find a target sum without int overflow.\nint l=0, r=a.length-1;\nwhile (l<r) {\n    long sum=(long)a[l]+a[r];\n    if (sum==target) break;\n    if (sum<target) l++; else r--;\n} ~~ 示例需有序a和target。含负数时某些按和伸缩的窗口算法不再正确。
dynamic-programming ~~ 动态规划 ~~ 明确状态、转移、初始值和计算顺序，通过保存重叠子问题结果减少重复计算。 ~~ static long ways(int n) {\n    if(n<0 || n>91) throw new IllegalArgumentException();\n    long a=1,b=1;\n    for(int i=2;i<=n;i++){long c=a+b;a=b;b=c;}\n    return b;\n} ~~ 示例计算每步走1或2级的方案数，n=0定义为1；上界防止long溢出。
backtracking ~~ 回溯与剪枝 ~~ 做选择、递归、撤销选择，枚举约束解。剪枝必须只删除不可能或已证明无需探索的分支。 ~~ static void subsets(int[] a,int i,java.util.List<Integer> path) {\n    if(i==a.length){System.out.println(path);return;}\n    subsets(a,i+1,path);\n    path.add(a[i]); subsets(a,i+1,path); path.remove(path.size()-1);\n} ~~ 输出子集需要O(2^n)数量级，额外路径和递归栈O(n)。每次递归返回都要恢复现场。
''','算法','结构与方法',SOURCE,'Java 21；片段中注明输入前提','algorithm.')
    return out
