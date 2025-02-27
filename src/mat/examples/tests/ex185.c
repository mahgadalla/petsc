static char help[] = "Tests MatCreateConstantDiagonal().\n"
"\n";

#include <petscmat.h>

/*T
    Concepts: Mat
T*/

int main(int argc, char **args)
{
  PetscErrorCode ierr;
  Vec            X, Y;
  Mat            A,Af;
  PetscReal      xnorm,ynorm;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;

  ierr = MatCreateConstantDiagonal(PETSC_COMM_WORLD,PETSC_DETERMINE,PETSC_DETERMINE,20,20,3.0,&A);CHKERRQ(ierr);
  ierr = MatCreateVecs(A,&X,&Y);CHKERRQ(ierr);
  ierr = MatView(A,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  ierr = VecSetRandom(X,NULL);CHKERRQ(ierr);
  ierr = VecNorm(X,NORM_2,&xnorm);CHKERRQ(ierr);
  ierr = MatMult(A,X,Y);CHKERRQ(ierr);
  ierr = VecNorm(Y,NORM_2,&ynorm);CHKERRQ(ierr);
  if (PetscAbsReal(ynorm - 3*xnorm) > PETSC_SMALL) SETERRQ2(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"Expected norm %g actual norm %g\n",(double)3*xnorm,(double)ynorm);CHKERRQ(ierr);
  ierr = MatShift(A,5.0);CHKERRQ(ierr);
  ierr = MatScale(A,.5);CHKERRQ(ierr);
  ierr = MatView(A,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = MatGetDiagonal(A,Y);CHKERRQ(ierr);

  ierr = MatGetFactor(A,MATSOLVERPETSC,MAT_FACTOR_LU,&Af);CHKERRQ(ierr);
  ierr = MatLUFactorSymbolic(Af,A,NULL,NULL,NULL);CHKERRQ(ierr);
  ierr = MatLUFactorNumeric(Af,A,NULL);CHKERRQ(ierr);
  ierr = MatSolve(Af,X,Y);CHKERRQ(ierr);
  ierr = VecNorm(Y,NORM_2,&ynorm);CHKERRQ(ierr);
  if (PetscAbsReal(ynorm - .25*xnorm) > PETSC_SMALL) SETERRQ2(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"Expected norm %g actual norm %g\n",(double).25*xnorm,(double)ynorm);CHKERRQ(ierr);

  ierr = MatDestroy(&A);CHKERRQ(ierr);
  ierr = MatDestroy(&Af);CHKERRQ(ierr);
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = VecDestroy(&Y);CHKERRQ(ierr);

  ierr = PetscFinalize();
  return ierr;
}

/*TEST

  test:
    nsize: 2

TEST*/
