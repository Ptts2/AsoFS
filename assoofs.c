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
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
static int assoofs_create_object(struct inode *dir , struct dentry *dentry, umode_t mode);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *actual, struct
assoofs_inode_info *search);

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

/*
* ppos desplazamientro respecto al principio del fichero
*/
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    char *buffer;
    int nbytes;

    printk(KERN_INFO "Read request\n");

    inode_info = (struct assoofs_inode_info*) filp->f_path.dentry->d_inode->i_private;

    if(*ppos >= inode_info->file_size) return 0;
    
    //Acceder al contenido del fichero
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);

    if(!bh) {
        printk(KERN_ERR "El intento de leer el bloque numero [%llu] fallo. \n", inode_info->data_block_number);
        return 0;
    }

    buffer = (char *)bh->b_data;

    //Copiar en buf buffer el contenido del fichero leido
    nbytes = min( (size_t)inode_info->file_size, len ); //Minimo entre el tamaño del fichero y lo que haya dicho el usuario
    
    if(copy_to_user(buf, buffer, nbytes)){ //Dir destino, direccion origen, cantidad de bytes
        
        brelse(bh);
        printk(KERN_ERR "Error copiando el contenido del archivo al espacio de usuario\n");
        return -1;
    } 
    
    *ppos += nbytes;
    brelse(bh);
    printk(KERN_INFO "Read request completed correctly \n");
    
    return nbytes;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct super_block *sb;

    char *buffer;
    
    printk(KERN_INFO "Write request\n");

    sb = filp->f_path.dentry->d_inode->i_sb;
    inode_info = (struct assoofs_inode_info*) filp->f_path.dentry->d_inode->i_private;

    //if(*ppos >= inode_info->file_size) return 0;

    //Acceder al contenido del fichero
    bh = sb_bread(sb, inode_info->data_block_number);

    buffer = (char *)bh->b_data;
    buffer +=*ppos;

    //Copiar en buffer buf el contenido del fichero escrito
    if(copy_from_user(buffer, buf, len)){ //Dir destino, direccion origen, cantidad de bytes
		brelse(bh);
		printk(KERN_ERR "Error en escribir desde el espacio de usuario al kernel\n");
		return -1;
	}

    *ppos += len;


    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    inode_info->file_size = *ppos;
    assoofs_save_inode_info(sb, inode_info);

    printk(KERN_INFO "Write request completed correctly \n");

	return len;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

/*
* dir_context se usa para representar el contenido de un directorio
*/
static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {

    struct inode *inode;
    struct super_block *sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    struct assoofs_inode_info *inode_info;

    int i;


    printk(KERN_INFO "Iterate request\n");

    if(ctx->pos) return 0; //Comprobar que el directorio esta creado en la cache, si ya esta salimos

    inode = filp->f_path.dentry->d_inode; //Tomamos el inodo del descriptor
    sb = inode->i_sb; //Tomamos el superbloque del inodo
    inode_info = inode->i_private; //Parte persistente del inodo

    if((!S_ISDIR(inode_info->mode))) return -1; //Si no es un directorio salimos

    bh = sb_bread(sb, inode_info->data_block_number); //Se lee el bloque
    record = (struct assoofs_dir_record_entry *)bh->b_data;

    for(i = 0; i< inode_info->dir_children_count; i++){

        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN); //Nombre del archivo y numero
        ctx->pos += sizeof(struct assoofs_dir_record_entry);
        record++;
    }

    brelse(bh);
    printk(KERN_INFO "Iterated correctly\n");
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
	inode_info = assoofs_get_inode_info(sb, ino);
	
	if(S_ISDIR(inode_info->mode)) //Si es un directorio
		inodo->i_fop = &assoofs_dir_operations; //Se asginan operaciones de directorio
	else if(S_ISREG(inode_info->mode)) //Si es un archivo
		inodo->i_fop = &assoofs_file_operations; //Se asginan operaciones de archivo
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
	
	struct assoofs_inode_info *parent_info; 
	struct super_block *sb; 
	struct buffer_head *bh; 
	struct assoofs_dir_record_entry *record;
    struct inode *inode;
	
	int i;
	
    parent_info = parent_inode->i_private;
    sb = parent_inode->i_sb; //Se toma el superbloque
	bh = sb_bread(sb, parent_info->data_block_number); //Bh para leer un bloque concreto
    
	printk(KERN_INFO "Lookup in: ino=%llu, b=%llu\n",parent_info->inode_no, parent_info->data_block_number);

	record = (struct assoofs_dir_record_entry*)bh->b_data;
	
	for(i = 0; i<parent_info->dir_children_count;i++){ //Recorro el bucle tantas veces como archivos tenga
		printk(KERN_INFO "Have file: '%s' (ino=%llu)\n", record->filename, record->inode_no);
		if(!strcmp(record->filename, child_dentry->d_name.name)){ //Se compara el nombre con el del argumento (devuelve 0 si son iguales)
			
			//Se guarda en memoria la información del inodo
			inode = assoofs_get_inode(sb, record->inode_no);
			
			inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info*)inode->i_private)->mode);
			d_add(child_dentry, inode); //Para construir el arbol de inodos
			return NULL;
		}
		
		printk(KERN_ERR "No se encontro inodo para el nombre [%s]\n", child_dentry->d_name.name);
		record++;
	}
	
    brelse(bh);
    return NULL;
}

/*
* Obtener un bloque libre
*/
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block){

    struct assoofs_super_block_info *assoofs_sb;
    int i;
   
    assoofs_sb = sb->s_fs_info;

    for(i = 2; i<ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
        if(assoofs_sb->free_blocks & (1<<i)){
            printk(KERN_INFO " El bloque numero %d esta libre", i);
            break;
        }

    if(i>= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "Error: No hay bloques libres");
        return -1;
    }

    *block = i;

    assoofs_sb->free_blocks &= ~(1 << i); //Marco el lugar como 0 en el mapa de bits
    assoofs_save_sb_info(sb);

    printk(KERN_INFO "Bloque libre obtenido correctamente");
    return 0;
}

/*
* Guardar informacion del superbloque en disco para que persista
*/
void assoofs_save_sb_info(struct super_block *vsb){

    struct buffer_head *bh;
    struct assoofs_super_block *sb; 

    sb = vsb->s_fs_info; //Informacion persistente
    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    bh->b_data = (char*)sb; //Se sobreescriben los datos con nuevos datos

    //Grabar en disco y liberar
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    printk(KERN_INFO "Informacion del superbloque actualizada correctamente");
}

/*
* Guardar en disco informacion persistente de un nuevo inodo
*/
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode){

    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;
    struct assoofs_super_block_info* assoofs_sb;

    assoofs_sb = sb->s_fs_info; 
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

    inode_info = (struct assoofs_inode_info*)bh->b_data;

    //Se busca el final del almacen de inodos y se copia en él el argumoento inode
    inode_info += assoofs_sb->inodes_count;
    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);

    //Guardar en disco para que persista
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    printk(KERN_INFO "Informacion de inodo guardada correctamente");

}

/*
* Actualizar en disco la informacion persistente de un inodo
*/
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info){

    struct buffer_head *bh;
    struct assoofs_inode_info *inode_pos;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER); //Obtener de disco el almacen de inodos

    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info*)bh->b_data, inode_info);

    if(inode_pos == NULL){
        printk(KERN_ERR "Error actualizando información de inodo");
        brelse(bh);
        return -1;
    }

    memcpy(inode_pos, inode_info, sizeof(*inode_pos)); //Se copia en el inodo buscado (inode_pos) la informacion del inodo actualizada
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    printk(KERN_INFO "Informacion del superbloque actualizada correctamente");
    return 0;
}

/*
* Busca un inodo en el almacen de inodos y lo devuelve
*/
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *actual, struct assoofs_inode_info *search){

    uint64_t count = 0;

    while(actual->inode_no != search->inode_no && count < ((struct assoofs_super_block_info*)sb->s_fs_info)->inodes_count){

        count++;
        actual++;
    }

    if(actual->inode_no == search->inode_no){
        printk(KERN_INFO "inodo encontrado");
        return actual;
    }else{
        printk(KERN_ERR "inodo no encontrado");
        return NULL;
    }
}

static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode){
    return assoofs_create_object(dir, dentry, S_IFDIR | mode);
}

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    return assoofs_create_object(dir, dentry, mode);
}

/*
* dir -> inodo que representa el directorio donde queremos crear el dir
* dentry -> directorio padre
*/
static int assoofs_create_object(struct inode *dir , struct dentry *dentry, umode_t mode) {

    struct inode *nodo;
    struct assoofs_inode_info *inode_info;
    struct super_block *sb;
    struct buffer_head *bh;

    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;

    uint64_t count;

   
    sb = dir->i_sb;
    count = ((struct assoofs_super_block_info*)sb->s_fs_info)->inodes_count; //Se obtiene el numero de inodos actual

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "Error: el numero máximo de archivos o directorios soportados (%d) se ha superado", ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED);
        return -1;
    }
    
    nodo = new_inode(sb); //Se crea el nuevo inodo

    nodo->i_sb = sb;
    nodo->i_op = &assoofs_inode_ops; 
    nodo->i_atime = nodo->i_mtime = nodo->i_ctime = current_time(nodo); 
    nodo->i_ino = count+1;

    //Información persistente del inodo en disco
    inode_info = kmalloc(sizeof(struct assoofs_inode_info),GFP_KERNEL);
    inode_info->inode_no = nodo->i_ino; 
    inode_info->mode = mode;

    if(S_ISDIR(mode)){ //Si es un directorio
        printk(KERN_INFO "New directory request\n");
        nodo->i_fop=&assoofs_dir_operations; //Operaciones de directorios
        inode_info->dir_children_count = 0;
    } else if(S_ISREG(mode)){ // Si es un archivo
         printk(KERN_INFO "New file request\n");
        nodo->i_fop=&assoofs_file_operations; //Operaciones de ficheros
        inode_info->file_size = 0;
    }

    nodo->i_private = inode_info; //Le asigno la informacion al inodo

    printk(KERN_INFO "inodo creado.");

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number); //Tomar el primer bloque libre
    assoofs_add_inode_info(sb, inode_info); //Informacion persistente de nodo a disco

    parent_inode_info = (struct assoofs_inode_info *) dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number); //Se lee el contenido en disco donde esta el dir padre

    dir_contents = (struct assoofs_dir_record_entry*)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count; //Muevo el puntero hasta llegar al final del directorio
    dir_contents->inode_no = inode_info->inode_no;

    strcpy(dir_contents->filename, dentry->d_name.name);

    //Escribir en disco
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    brelse(bh);

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info); //Pasar informacion a disco


    inode_init_owner(nodo, dir, mode);
    d_add(dentry, nodo);

    printk(KERN_INFO "Inodo creado y añadido correctamente");

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
    
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  
    // 2.- Comprobar los parámetros del superbloque
    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)

    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;
    struct inode *root_inode;
    
    printk(KERN_INFO "assoofs_fill_super request\n");
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
    
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    struct dentry *ret;

    printk(KERN_INFO "assoofs_mount request\n");
    ret =  mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

    if(IS_ERR(ret)){
        printk(KERN_ERR "Error montando el sistema de ficheros assoofs");
        return NULL;
    }else{
        printk(KERN_INFO "assoofs montado correctamente");
        return ret;
    }    
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

    int ret;
    printk(KERN_INFO "assoofs_init request\n");
    ret = register_filesystem(&assoofs_type);

    if(ret == 0)
        printk(KERN_INFO "assoofs registrado correctamente");
    else
        printk(KERN_ERR "Error registrando el sistema de ficheros assoofs");
    return ret;
    
}

static void __exit assoofs_exit(void) {

    int ret;
    printk(KERN_INFO "assoofs_exit request\n");

    ret = unregister_filesystem(&assoofs_type);
    
    if(ret == 0)
        printk(KERN_INFO "assoofs desregistrado correctamente");
    else
        printk(KERN_ERR "Error desregistrando el sistema de ficheros assoofs");
    
}

module_init(assoofs_init);
module_exit(assoofs_exit);
