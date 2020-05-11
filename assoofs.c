#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"


/**
* Funciones auxiliares
*/
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);

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

static struct inode *assoofs_get_inode(struct super_block *sb, int ino){
	
	struct assoofs_inode_info *inode_info;
	struct inode *inodo;
	
	inodo = new_inode(sb);
	inode_info = assoofs_get_inode_info(sb,ino);
	
	if(S_ISDIR(inode_info->mode)) //Si es un directorio
		inodo->i_fop = inodo->i_fop = &assoofs_dir_operations; //Se asginan operaciones de directorio
	else if(S_ISREG(inode_info->mode)) //Si es un archivo
		inodo->i_fop = inodo->i_fop = &assoofs_file_operations; //Se asginan operaciones de archivo
	else
		printk(KERN_ERR "Error en el tipo de inodo: no es directorio ni archivo.");
	
	inodo->i_ino = ino; //Se asigna numero de inodo
    inodo->i_sb = sb; //Se asigna un puntero al superbloque
    inodo->i_op = &assoofs_inode_ops; //Se asignan operaciones de inodo
    inodo->i_atime = inodo->i_mtime = inodo->i_ctime = current_time(inodo); //Se le asignan las fechas (acceso, modificacion y creacion)
	inodo->i_private = inode_info;
	
	
	return inodo;
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
	
	struct assoofs_inode_info *parent_info = parent_inode->i_private; 
	struct super_block *sb = parent_inode->i_sb; //Se toma el superbloque
	struct buffer_head *bh; //Bh para leer un bloque concreto
	struct assoofs_dir_record_entry *record;
	
	int i;
	
	bh = sb_bread(sb, parent_info->data_block_number);
	printk(KERN_INFO "Lookup in: ino=%llu, b=%llu\n",parent_info->inode_no, parent_info->data_block_number);

	record = (struct assoofs_dir_record_entry*)bh->b_data;
	
	for(i = 0; i<parent_info->dir_children_count;i++){ //Recorro el bucle tantas veces como archivos tenga
		printk(KERN_INFO "Have file: '%s' (ino=%llu)\n", record->filename, record->inode_no);
		if(!strcmp(record->filename, child_dentry->d_name.name)){ //Se compara el nombre con el del argumento (devuelve 0 si son iguales)
			
			//Se guarda en memoria la información del inodo
			struct inode *inode = assoofs_get_inode(sb, record->inode_no);
			
			inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info*)inode->i_private)->mode);
			d_add(child_dentry, inode); //Para construir el arbol de inodos
			return NULL;
		}
		
		printk(KERN_ERR "No se encontro inodo para el nombre [%s]\n", child_dentry->d_name.name);
		record++;
	}
	
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
    inode_info = (struct assoofs_inode_info*)bh->b_data;

    //Se busca un inodo con numero inode_no
    for(i = 0; i<afs_sb->inodes_count;i++){

        if(inode_info->inode_no == inode_no) {

            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL); //Se asigna memoria al buffer
            memcpy(buffer, inode_info, sizeof(*buffer)); //Se copia el contenido de inode_info en buffer
            break;
        }
        inode_info++; //Pasa a apuntar al siguiente elemento
    }

    brelse(bh); //Se liberan recursos
    return buffer;
}

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  
    // 2.- Comprobar los parámetros del superbloque
    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)

    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;

    struct inode *root_inode;
    

    //1
    bh=sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); //Segundo arg bloque donde se almacenará el superbloque declarado en el archivo de cabecera
    assoofs_sb = (struct assoofs_super_block_info*)bh->b_data; //Se toman los datos del bloque de la funcion y se asignan a otra variable

    //2
    if(assoofs_sb->magic != ASSOOFS_MAGIC || assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
    {
        printk(KERN_ERR "assoofs superblock invalid parameters");
        return -1;
    }

    //3
    sb->s_magic = ASSOOFS_MAGIC; //Se asigna el numero magico
	sb->s_fs_info = assoofs_sb; //Contenido del superbloque
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE; //Se asigna el tamaño de bloque
    sb->s_op = &assoofs_sops; //Se asignan las operaciones

    //4
	root_inode = new_inode(sb);
    inode_init_owner(root_inode, NULL, S_IFDIR); //se asignan los permisos, NULL porque es el dir raiz, no tiene dir padre, S_IFDIR para directorio, S_IFREG para fichero en 3er argumento

    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; //Se asigna numero de inodo
    root_inode->i_sb = sb; //Se asigna un puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; //Se asignan operaciones de inodo
    root_inode->i_fop = &assoofs_dir_operations; //Se asginan operaciones de directorio
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); //Se le asignan las fechas (acceso, modificacion y creacion)

    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); //La informacion persistente VER ERROR

    sb->s_root = d_make_root(root_inode); //Lo marco como nodo raiz
	
    brelse(bh); //Se libera la memoria de bh
    return 0;
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
