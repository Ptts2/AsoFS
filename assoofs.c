#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    return 0;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Write request\n");
    return 0;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    printk(KERN_INFO "Iterate request\n");
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");
    return NULL;
}


static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    return 0;
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  
    // 2.- Comprobar los parámetros del superbloque
    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)


    //1
    struct buffer_head *bh;
    struct assofs_superblock_info *assofs_sb;

    bh=sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); //Segundo arg bloque donde se almacenará el superbloque declarado en el archivo de cabecera
    assofs_sb = (struct asso_superblock_info*)bh->b_data; //Se toman los datos del bloque de la funcion y se asignan a otra variable

    brelse(bh); //Se libera la memoria de bh    

    //2
    if(assofs_sb->magic != ASSOOFS_MAGIC || assofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
    {
        printk(KERN_WARNING "assoofs superblock invalid parameters");
        return -1;
    }


    //3
    sb->s_magic = ASSOOFS_MAGIC; //Se asigna el numero magico
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE; //Se asigna el tamaño de bloque
    sb->s_op = &assoofs_sops; //Se asignan las operaciones

    //4

    struct inode *root_inode;
    root_inode = new_inode(sb);

    inode_init_owner(root_inode, NULL, S_IFDIR); //se asignan los permisos, NULL porque es el dir raiz, no tiene dir padre, S_IFDIR para directorio, S_IFREG para fichero en 3er argumento

    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; //Se asigna numero de inodo
    root_inode->i_sb = sb; //Se asigna un puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; //Se asignan operaciones de inodo
    root_inode->i_fop = &assoofs_dir_operations; //Se asginan operaciones de directorio
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); //Se le asignan las fechas (acceso, modificacion y creacion)
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOFS_ROOTDIR_INODE_NUMBER); //La informacion persistente


    sb->s_root = d_make_root(root_inode); //Lo marco como nodo raiz

    return 0;
}

/*
 *  Obtener informacion persistente del inodo
 */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no) {

    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;

    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;

    int i;

    //Se lee el bloque con el almacen de inodos
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info)bh->b_data;

    //Se busca un inodo con numero inode_no
    for(i = 0; i<afs_sb->inodes_count;i++){

        if(inode_info->inode_no == inode_no) {

            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL); //Se asigna memoria al buffer
            memcpy(buffer, inode_info, sizeof(*buffer)); //Se copia el contenido de inode_info en buffer
            break;
        }
        inode_info++;
    }

    brelse(bh); //Se liberan recursos
    return buffer;
}

/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    printk(KERN_INFO "assoofs_mount request\n");
    struct dentry *ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
}

/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,
    .name    = "assoofs",
    .mount   = assoofs_mount,
    .kill_sb = kill_litter_super,
};

static int __init assoofs_init(void) {
    printk(KERN_INFO "assoofs_init request\n");
    int ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}

static void __exit assoofs_exit(void) {
    printk(KERN_INFO "assoofs_exit request\n");
    int ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}

module_init(assoofs_init);
module_exit(assoofs_exit);
