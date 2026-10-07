final class Splay {
    record Leaf(int[] array,String text){}
    record Branch(Object left,Object right){}
    static final class Node {final double key;final Object payload;Node left,right;Node(double k,Object p){key=k;payload=p;}}
    static final class State {Node root;long seed=49734321;
        double random(){long hi=seed/127773,lo=seed%127773;seed=16807*lo-2836*hi;if(seed<=0)seed+=2147483647;return seed/2147483647.0;}
        double insert(){double key;do{key=random();root=splay(root,key);}while(root!=null&&root.key==key);
            Object value=payload(5,key);Node n=new Node(key,value);
            if(root!=null){if(key>root.key){n.left=root;n.right=root.right;root.right=null;}
                else{n.right=root;n.left=root.left;root.left=null;}}root=n;return key;}
        void remove(double key){root=splay(root,key);NativeBench.check(root!=null&&root.key==key);
            if(root.left==null)root=root.right;else{Node right=root.right;root=splay(root.left,key);root.right=right;}}
    }
    static Object payload(int depth,double tag){if(depth==0)return new Leaf(new int[]{0,1,2,3,4,5,6,7,8,9},"String for key "+tag+" in leaf node");
        return new Branch(payload(depth-1,tag),payload(depth-1,tag));}
    static Node splay(Node root,double key){if(root==null)return null;
        Node dummy=new Node(0,null),left=dummy,right=dummy,current=root;
        while(true){if(key<current.key){if(current.left==null)break;
                if(key<current.left.key){Node tmp=current.left;current.left=tmp.right;tmp.right=current;current=tmp;if(current.left==null)break;}
                right.left=current;right=current;current=current.left;
            }else if(key>current.key){if(current.right==null)break;
                if(key>current.right.key){Node tmp=current.right;current.right=tmp.left;tmp.left=current;current=tmp;if(current.right==null)break;}
                left.right=current;left=current;current=current.right;
            }else break;}
        left.right=current.left;right.left=current.right;current.left=dummy.right;current.right=dummy.left;return current;
    }
    static State prepare(){State s=new State();for(int i=0;i<8000;i++)s.insert();return s;}
    static State work(State s,int repeats){for(int r=0;r<repeats;r++)for(int i=0;i<80;i++){
        double key=s.insert();s.root=splay(s.root,key);Node greatest=null;
        if(s.root.key<key)greatest=s.root;else if(s.root.left!=null){greatest=s.root.left;while(greatest.right!=null)greatest=greatest.right;}
        s.remove(greatest==null?key:greatest.key);}return s;}
    static void verify(State s){java.util.ArrayDeque<Node> stack=new java.util.ArrayDeque<>();Node n=s.root;double last=Double.NEGATIVE_INFINITY;int count=0;
        while(n!=null||!stack.isEmpty()){while(n!=null){stack.push(n);n=n.left;}n=stack.pop();NativeBench.check(n.key>last);last=n.key;count++;n=n.right;}NativeBench.check(count==8000);}
    static void run(int repeats){NativeBench.prepared(Splay::prepare,s->work(s,repeats),Splay::verify,v->{});}
}
